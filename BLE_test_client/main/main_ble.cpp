/**
 * Create a new BLE server.
 */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/event_groups.h"
#include "nvs_flash.h"
#include "esp_ota_ops.h"
#include "driver/gpio.h"

#include "esp_bt_main.h"
#include "BLEDevice.h"
#include "BLEServer.h"
#include "BLEUtils.h"
#include "BLE2902.h"
#include "BLE2904.h"
#include "BLEUUID.h"
#include "BLEScan.h"
#include <esp_log.h>
#include <string>
#include <string.h>
#include <Task.h>
#include "http_client.h"

#include "sdkconfig.h"

#define SSID        "sipeed"
#define PASSWORD    "12345678"
#define DEBUG_BLE   0

static BLEUUID    serviceUUID("424c454f-5441-0001-0000-006573703332");
static BLEUUID    charUUIDRX("424c454f-5441-0002-0000-006573703332");
static BLEUUID    charUUID("424c454f-5441-0003-0000-006573703332");

#define LOG_TAG __func__
// static char LOG_TAG[] = "BLE-OTA-Server";
BLEService* pService;
BLEClient* pxClient;
BLERemoteCharacteristic *pRemChar;
bool connected = false;
BLEScan* pBLEScan;

#include "soc/rtc.h"
#include "esp32/clk.h"
ota_file_t* file;
static size_t step = 500;

static void notifyCallback(
	BLERemoteCharacteristic* pBLERemoteCharacteristic,
	uint8_t* pData,
	size_t length,
	bool isNotify) {
		ESP_LOGD(LOG_TAG, "Notify callback for characteristic %s of data %s length %d",
				pBLERemoteCharacteristic->getUUID().toString().c_str(), ((char*) pData), length);
        ESP_LOGI(__func__, "stack highwater--> %d", uxTaskGetStackHighWaterMark(NULL));

        // ota_chunk(pData, length);
		// indications are sent in 500 chunks and last one usually is shorter,
		// in very rare case this may fail, but this is only example so i dont care
        // if(length < 500)
        //     ota_finalize();
}

void task(void *p){
    uint64_t current = rtc_time_get();
    uint64_t last = rtc_time_get();
    uint64_t bytes = 0;
    size_t total_size = file->size;
    ESP_LOGE(__func__, "size: %d", total_size);
    ESP_LOG_BUFFER_HEX_LEVEL(__func__, file->response, 32, ESP_LOG_INFO);
    // while(connected && bytes < total_size)
    // {
    //     size_t size = (file->size - bytes < step) ? file->size - bytes : step;
    //     current = rtc_time_get();
    //     pChar->setValue(file->response + bytes, size);
    //     pChar->notify(false);
    //     last = current;
    //     bytes += size;
    //     ESP_LOGI(__func__, "Bytes: %lld, speed: ", bytes);
    // }
    ESP_LOGE(__func__, "size: %lld", bytes);
    vTaskDelete(NULL);
}

class MyCallbacks : public BLEClientCallbacks{
	void onConnect(BLEClient *pClient)
	{
        connected = true;

		ESP_LOGI(LOG_TAG, "onConnect");
        file = get_ota_file();
        // ESP_LOGW(__func__, "MTU: %d", pClient->getPeerMTU(pClient->getConnId()));
	}

	void onDisconnect(BLEClient *pClient)
	{
		ESP_LOGI(LOG_TAG, "onDisconnect");
        connected = false;
	}

};


static esp_ble_addr_type_t type;
class MyClient: public Task {
	void run(void* data) {
 		ESP_LOGI(LOG_TAG, "Advertised Device: %s", ((BLEAddress*)data)->toString().c_str());
		BLEAddress addr = *(BLEAddress*)data;
        BLEClient*  pClient  = BLEDevice::createClient();
        pClient->setClientCallbacks(new MyCallbacks());
        pClient->connect(addr, type);

		BLERemoteService* pRemoteService = pClient->getService(serviceUUID);
		if (pRemoteService == nullptr) {
			ESP_LOGD(LOG_TAG, "Failed to find our service UUID: %s", serviceUUID.toString().c_str());
			pClient->disconnect();
			stop();
			return;
		}

		BLERemoteCharacteristic* pRemoteCharacteristic = pRemoteService->getCharacteristic(charUUID);
		if (pRemoteCharacteristic == nullptr) {
			ESP_LOGW(LOG_TAG, "Not found our characteristic UUID: %s", charUUID.toString().c_str());
			pClient->disconnect();
			stop();
			return;
		}

		pRemoteCharacteristic->registerForNotify(notifyCallback, false);

		pRemoteCharacteristic = pRemoteService->getCharacteristic(charUUIDRX);
		if (pRemoteCharacteristic == nullptr) {
			ESP_LOGW(LOG_TAG, "Not found our characteristic UUID: %s", charUUIDRX.toString().c_str());
			pClient->disconnect();
			stop();
			return;
		}

        pRemChar = pRemoteCharacteristic;
        
		ESP_LOGD(LOG_TAG, "%s", pClient->toString().c_str());
		ESP_LOGD(LOG_TAG, "-- End of task");
		stop();
	} // run
}; // MyClient


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

class MyAdvertisedDeviceCallbacks: public BLEAdvertisedDeviceCallbacks {
	void onResult(BLEAdvertisedDevice advertisedDevice) {
		ESP_LOGI(LOG_TAG, "Advertised Device: %s", advertisedDevice.toString().c_str());
		ESP_LOG_BUFFER_HEX_LEVEL(__func__, advertisedDevice.getPayload(), advertisedDevice.getPayloadLength(), ESP_LOG_WARN);
		if (advertisedDevice.haveServiceUUID() && advertisedDevice.isAdvertisingService(BLEUUID((uint16_t)0x1234))) {
 			pBLEScan->stop();  // <--- it is required to always stop scan before we try to connect to another device, if we wont stop app will stall in esp-idf bt stack
            type = advertisedDevice.getAddressType();

            MyClient* pMyClient = new MyClient();
            pMyClient->setStackSize(5000);
            pMyClient->start(new BLEAddress(*advertisedDevice.getAddress().getNative()));
		} 
	} 
}; 

static void scan1(void*){
	ESP_LOGI(LOG_TAG, "start scan");
	pBLEScan->start(0, true);
    ESP_LOGI(LOG_TAG, "scan stop success");
    
	vTaskDelete(NULL);
}

class MainBLEServer: public Task {
	void run(void *data) {
		ESP_LOGD(LOG_TAG, "Starting BLE work!");

        BLEDevice::init("esp32");
        BLEDevice::setMTU(517);
        nvs_flash_erase();
    #if DEBUG_BLE
        BLEDevice::setCustomGapHandler(my_gap_event_handler);
        BLEDevice::setCustomGattsHandler(my_gatts_event_handler);
        BLEDevice::setCustomGattcHandler(my_gattc_event_handler);
    #endif

        BLEDevice::setMTU(517);
        pBLEScan = BLEDevice::getScan();
        pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
        pBLEScan->setActiveScan(true);
        pBLEScan->setInterval(1389);
        pBLEScan->setWindow(349);

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
    esp_log_level_set("BLEUtils", ESP_LOG_NONE);
    esp_log_level_set("HTTP_CLIENT", ESP_LOG_WARN);

    esp_err_t err = nvs_flash_init();
    if (err != ESP_OK) {
        ESP_ERROR_CHECK( nvs_flash_erase() );
        ESP_ERROR_CHECK( nvs_flash_init() );
    }

    initialise_wifi();
	MainBLEServer* pMainBleServer = new MainBLEServer();
	pMainBleServer->setStackSize(8000);
	pMainBleServer->start();

    xTaskCreate(http_client, "httpClient", 4*1024, NULL, 5, NULL);
 } // app_main


extern "C" void start_scan()
{
    xTaskCreate(scan1, "scan", 4048, NULL, 5, NULL);
    gpio_set_pull_mode(GPIO_NUM_0, GPIO_PULLUP_ONLY);

// start OTA update on press BOOT putton
    while(gpio_get_level(GPIO_NUM_0))
    {
        vTaskDelay(50);
    }
    ESP_LOGI(__func__, "gpio down");

    ota_file_t* file = get_ota_file();
    uint8_t* buf = file->response;
    uint32_t bytes_total = file->size, bytes_sent = 0;
    uint16_t bytes = step;
    do
    {
        if(bytes_total - bytes_sent < step)
            bytes = bytes_total - bytes_sent;

        pRemChar->writeValue(buf + bytes_sent, bytes);

        bytes_sent += bytes;
        ESP_LOGW(LOG_TAG, "bytes => %d, sent => %d, total => %d", bytes, bytes_sent, bytes_total);
        vTaskDelay(1);
    }while(bytes >= step);
}