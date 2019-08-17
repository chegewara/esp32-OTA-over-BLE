/**
 * Create a new BLE server.
 */
#include "esp_wifi.h"
#include "esp_event_loop.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_event_loop.h"
#include "nvs_flash.h"

#include "esp_bt_main.h"
#include "BLEDevice.h"
#include "BLEServer.h"
#include "BLEUtils.h"
#include "BLE2902.h"
#include "BLE2904.h"
#include "BLEUUID.h"
#include <esp_log.h>
#include <string>
#include <string.h>
#include <Task.h>
#include "http_client.h"

#include "sdkconfig.h"

#define SSID        "sipeed"
#define PASSWORD    "12345678"
#define DEBUG_BLE   0

static BLEUUID    servUUID("424c454f-5441-0001-0000-006573703332");
static BLEUUID    charUUIDRX("424c454f-5441-0002-0000-006573703332");
static BLEUUID    charUUIDTX("424c454f-5441-0003-0000-006573703332");

static char LOG_TAG[] = "BLE-OTA-Server";
BLEService* pService;
BLEServer* pServer;
BLECharacteristic *pChar;
BLECharacteristicCallbacks *p_myCallbacks;
bool connected = false;

#include "soc/rtc.h"
#include "esp32/clk.h"
ota_file_t* file;
static size_t step = 500;

void task(void *p){
    uint64_t current = rtc_time_get();
    uint64_t last = rtc_time_get();
    uint64_t bytes = 0;
    size_t total_size = file->size;
    ESP_LOGE(__func__, "size: %d", total_size);
    ESP_LOG_BUFFER_HEX_LEVEL(__func__, file->response, 32, ESP_LOG_INFO);
    while(connected && bytes < total_size)
    {
        size_t size = (file->size - bytes < step) ? file->size - bytes : step;
        current = rtc_time_get();
        pChar->setValue(file->response + bytes, size);
        pChar->notify(false);
        last = current;
        bytes += size;
        ESP_LOGI(__func__, "Bytes: %lld, speed: ", bytes);
    }
    ESP_LOGE(__func__, "size: %lld", bytes);
    vTaskDelete(NULL);
}

class MyCallbacks : public BLEServerCallbacks{
	void onConnect(BLEServer *pServer, esp_ble_gatts_cb_param_t *param)
	{
        connected = true;
		pServer->updateConnParams(param->connect.remote_bda, 0x06, 0x06, 0, 100);

		ESP_LOGI(LOG_TAG, "onConnect");
        file = get_ota_file();
        ESP_LOGW(__func__, "MTU: %d", pServer->getPeerMTU(pServer->getConnId()));
	}

	void onDisconnect(BLEServer *pServer)
	{
		ESP_LOGI(LOG_TAG, "onDisconnect");
        connected = false;
	}

};

class MyCharacteristicCallback : public BLECharacteristicCallbacks {

	void onWrite(BLECharacteristic* pChar) 
    {
		ESP_LOGI(LOG_TAG, "write to char: ");
		xTaskCreate(task, "task", 512*16, NULL, 6, NULL);
	}

    void onRead(BLECharacteristic* pChar)
    {
        uint8_t* app_desriptor = get_ota_version();        
        pChar->setValue(app_desriptor, 256);
    }
};

#if DEBUG_BLE
static void my_gap_event_handler(esp_gap_ble_cb_event_t  event, esp_ble_gap_cb_param_t* param) {
	ESP_LOGW(LOG_TAG, "custom gap event handler, event: %d", (uint8_t)event);
}

static void my_gattc_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if, esp_ble_gattc_cb_param_t* param) {
	ESP_LOGW(LOG_TAG, "custom gattc event handler, event: %d", (uint8_t)event);
}

static void my_gatts_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gattc_if, esp_ble_gatts_cb_param_t* param) {
	ESP_LOGW(LOG_TAG, "custom gatts event handler, event: %d", (uint8_t)event);
}
#endif

class MainBLEServer: public Task {
	void run(void *data) {
		ESP_LOGD(LOG_TAG, "Starting BLE work!");

#if DEBUG_BLE
		BLEDevice::setCustomGapHandler(my_gap_event_handler);
		BLEDevice::setCustomGattsHandler(my_gatts_event_handler);
		BLEDevice::setCustomGattcHandler(my_gattc_event_handler);
#endif

		BLEDevice::init("OTAoverBLE");
		BLEDevice::setMTU(517);
		pServer = BLEDevice::createServer();
		pServer->setCallbacks(new MyCallbacks());
		pService = pServer->createService(servUUID);

		BLECharacteristic* pCharacteristic = pService->createCharacteristic(
			charUUIDTX, BLECharacteristic::PROPERTY_INDICATE // | BLECharacteristic::PROPERTY_NOTIFY
		);
        pChar = pCharacteristic;
		BLE2902* p2902Descriptor = new BLE2902();
		BLE2904* p2904Descriptor = new BLE2904();
		pCharacteristic->addDescriptor(p2902Descriptor);
		pCharacteristic->addDescriptor(p2904Descriptor);
        BLECharacteristic* pCharacteristic2 = pService->createCharacteristic(
            charUUIDRX, BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_READ
        );
		pCharacteristic2->setCallbacks(p_myCallbacks);
		pService->start();

		BLEAdvertising* pAdvertising = BLEDevice::getAdvertising();
		pAdvertising->addServiceUUID(servUUID);
		pAdvertising->setMinPreferred(0x06);
		pAdvertising->setMaxPreferred(0x06);
		pAdvertising->setScanResponse(true);
        pAdvertising->addServiceUUID("1234");
        BLEDevice::startAdvertising();
		ESP_LOGD(LOG_TAG, "Advertising started!");
	}
};

static EventGroupHandle_t s_wifi_event_group;


const int CONNECTED_BIT = BIT0;
static esp_err_t event_handler(void *ctx, system_event_t *event)
{
    switch (event->event_id) {
        case SYSTEM_EVENT_STA_START:
            esp_wifi_connect();
            break;
        case SYSTEM_EVENT_STA_GOT_IP:
            xEventGroupSetBits(s_wifi_event_group, CONNECTED_BIT);
            break;
        case SYSTEM_EVENT_STA_DISCONNECTED:
            esp_wifi_connect();
            xEventGroupClearBits(s_wifi_event_group, CONNECTED_BIT);
            break;
        default:
            break;
    }
    return ESP_OK;
}

static void initialise_wifi(void)
{
    tcpip_adapter_init();
    s_wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK( esp_event_loop_init(event_handler, NULL) );
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK( esp_wifi_init(&cfg) );
    ESP_ERROR_CHECK( esp_wifi_set_storage(WIFI_STORAGE_RAM) );
    wifi_config_t* wifi_config = (wifi_config_t*)calloc(1, sizeof(wifi_config_t));
    char ssid[] = SSID;
    char pass[] = PASSWORD;
    memcpy(wifi_config->sta.ssid, ssid, strlen(ssid));
    memcpy(wifi_config->sta.password, pass, strlen(pass));
    ESP_ERROR_CHECK( esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK( esp_wifi_set_config(WIFI_IF_STA, wifi_config) );
    ESP_ERROR_CHECK( esp_wifi_start() );
    ESP_LOGI(LOG_TAG, "Connecting to \"%s\"", wifi_config->sta.ssid);
    xEventGroupWaitBits(s_wifi_event_group, CONNECTED_BIT, false, true, portMAX_DELAY);
    ESP_LOGI(LOG_TAG, "Connected");
}

void OTA_over_BLE(void)
{
	esp_log_level_set("*", ESP_LOG_WARN);
    esp_err_t err = nvs_flash_init();
    if (err != ESP_OK) {
        ESP_ERROR_CHECK( nvs_flash_erase() );
        ESP_ERROR_CHECK( nvs_flash_init() );
    }

    initialise_wifi();
	p_myCallbacks = new MyCharacteristicCallback();
	MainBLEServer* pMainBleServer = new MainBLEServer();
	pMainBleServer->setStackSize(8000);
	pMainBleServer->start();

    xTaskCreate(http_client, "httpClient", 4*1024, NULL, 5, NULL);
 } // app_main
