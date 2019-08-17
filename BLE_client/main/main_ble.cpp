#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <string>
#include <sstream>
#include <sys/time.h>
#include "BLEDevice.h"
#include "FreeRTOS.h"
#include <nvs_flash.h>
#include "esp_ota_ops.h"
#include "driver/gpio.h"

#include "BLEAdvertisedDevice.h"
#include "BLEClient.h"
#include "BLEScan.h"
#include "BLEUtils.h"
#include "Task.h"

#include "sdkconfig.h"
#define TICKS_TO_DELAY 1000
static BLEScan *pBLEScan;
static const char* LOG_TAG = "OTA-BLE-client";
static BLERemoteCharacteristic* pRemChar = nullptr;

static BLEUUID    serviceUUID("424c454f-5441-0001-0000-006573703332");
static BLEUUID    charUUIDRX("424c454f-5441-0002-0000-006573703332");
static BLEUUID    charUUID("424c454f-5441-0003-0000-006573703332");

static void scan1(void*);
extern "C" void ota_begin();
extern "C" void ota_chunk(uint8_t* text, size_t buff_len);
extern "C" void ota_finalize();
extern "C" bool compare_app_versions(esp_app_desc_t* app_descriptor);

static void notifyCallback(
	BLERemoteCharacteristic* pBLERemoteCharacteristic,
	uint8_t* pData,
	size_t length,
	bool isNotify) {
		ESP_LOGD(LOG_TAG, "Notify callback for characteristic %s of data %s length %d",
				pBLERemoteCharacteristic->getUUID().toString().c_str(), ((char*) pData), length);
        ESP_LOGI(__func__, "stack highwater--> %d", uxTaskGetStackHighWaterMark(NULL));

        ota_chunk(pData, length);
		// indications are sent in 500 chunks and last one usually is shorter,
		// in very rare case this may fail, but this is only example so i dont care
        if(length < 500)
            ota_finalize();
}

class MyCallbacks : public BLEClientCallbacks {
	void onConnect(BLEClient* pC){
	}
	void onDisconnect(BLEClient* pC) {
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
        pRemChar->readValue();
        esp_app_desc_t* app_descriptor = (esp_app_desc_t*)pRemChar->getRawData();
        if(compare_app_versions(app_descriptor))
        {
            // we have the same app version running, no need to update
            ESP_LOGD(LOG_TAG, "%s", pClient->toString().c_str());
            ESP_LOGW(LOG_TAG, "-- Firmware already installed.");
            stop();
            return;
        }

        ota_begin();
        
		ESP_LOGD(LOG_TAG, "%s", pClient->toString().c_str());
		ESP_LOGD(LOG_TAG, "-- End of task");
		stop();
	} // run
}; // MyClient


class MyAdvertisedDeviceCallbacks: public BLEAdvertisedDeviceCallbacks {
	void onResult(BLEAdvertisedDevice advertisedDevice) {
		ESP_LOGI(LOG_TAG, "Advertised Device: %s", advertisedDevice.toString().c_str());
		ESP_LOG_BUFFER_HEX_LEVEL(__func__, advertisedDevice.getPayload(), advertisedDevice.getPayloadLength(), ESP_LOG_WARN);
		if (advertisedDevice.haveServiceUUID() && advertisedDevice.isAdvertisingService(BLEUUID("1234"))) {
 			pBLEScan->stop();  // <--- it is required to always stop scan before we try to connect to another device, if we wont stop app will stall in esp-idf bt stack
            type = advertisedDevice.getAddressType();

            MyClient* pMyClient = new MyClient();
            pMyClient->setStackSize(5000);
            pMyClient->start(new BLEAddress(*advertisedDevice.getAddress().getNative()));
		} 
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

static void internal_task(void* p)
{
	esp_log_level_set("*", ESP_LOG_WARN);
	esp_log_level_set("BLEUtils", ESP_LOG_NONE);

	BLEDevice::init("esp32");
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

	xTaskCreate(scan1, "scan", 4048, NULL, 5, NULL);
    gpio_set_pull_mode(GPIO_NUM_0, GPIO_PULLUP_ONLY);

// start OTA update on press BOOT putton
    while(gpio_get_level(GPIO_NUM_0))
    {
        vTaskDelay(50);
    }

    ESP_LOGI(__func__, "gpio down");
    pRemChar->writeValue(5);

    vTaskDelete(NULL);
} // SampleClient

static void scan1(void*){
	ESP_LOGI(LOG_TAG, "start scan");
	pBLEScan->start(0, true);
    ESP_LOGI(LOG_TAG, "scan stop success");
    
	vTaskDelete(NULL);
}

void OTA_over_BLE_Client() 
{
    xTaskCreate(internal_task, "intTask", 10000, NULL, 5, NULL);
}
