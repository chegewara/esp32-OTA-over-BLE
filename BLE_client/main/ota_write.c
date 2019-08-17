#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event_loop.h"
#include "esp_log.h"
#include "esp_ota_ops.h"

#include "nvs.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

static char* TAG = "OTA_WRITE";

static int binary_file_length = 0;
static esp_ota_handle_t update_handle = 0 ;
static const esp_partition_t *update_partition = NULL;

static void task_fatal_error()
{
    ESP_LOGE(__func__, "FATAL ERROR");
    while(1);
}

/**
 * Prepare OTA partition to perform update
 */
void ota_begin()
{
    esp_err_t err;
    /* update handle : set by esp_ota_begin(), must be freed via esp_ota_end() */

    ESP_LOGE(TAG, "Starting OTA example...");

    const esp_partition_t *configured = esp_ota_get_boot_partition();
    const esp_partition_t *running = esp_ota_get_running_partition();

    if (configured != running) {
        ESP_LOGW(TAG, "Configured OTA boot partition at offset 0x%08x, but running from offset 0x%08x",
                 configured->address, running->address);
        ESP_LOGW(TAG, "(This can happen if either the OTA boot data or preferred boot image become corrupted somehow.)");
    }
    ESP_LOGI(TAG, "Running partition type %d subtype %d (offset 0x%08x)",
             running->type, running->subtype, running->address);

    ESP_LOGI(TAG, "Connect to Wifi ! Start to Connect to Server....");


    update_partition = esp_ota_get_next_update_partition(NULL);
    assert(update_partition != NULL);
    ESP_LOGI(TAG, "Writing to partition subtype %d at offset 0x%x",
             update_partition->subtype, update_partition->address);

    err = esp_ota_begin(update_partition, OTA_SIZE_UNKNOWN, &update_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin failed, error=%d", err);
        task_fatal_error();
    }
    ESP_LOGI(TAG, "esp_ota_begin succeeded");
}

/**
 * Flash chunks received from BLE server in indication/notification  (eventually read from server)
 */
void ota_chunk(uint8_t* text, size_t buff_len) 
{
    ESP_LOG_BUFFER_HEX_LEVEL(__func__, text, 32, ESP_LOG_INFO);
    esp_err_t err = esp_ota_write( update_handle, (const void *)text, buff_len);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error: esp_ota_write failed! err=0x%x", err);
        task_fatal_error();
    }
    binary_file_length += buff_len;
}

/**
 * Finalize OTA update and restart
 */
void ota_finalize() 
{
    ESP_LOGI(TAG, "Total Write binary data length : %d", binary_file_length);

    if (esp_ota_end(update_handle) != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_end failed!");
        task_fatal_error();
    }
    esp_err_t err = esp_ota_set_boot_partition(update_partition);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition failed! err=0x%x", err);
        task_fatal_error();
    }
    ESP_LOGI(TAG, "Prepare to restart system!");
    esp_restart();
    return ;
}

/**
 * Compare esp_app_desc_t currently running app with value read from BLE server (new app version)
 */
bool compare_app_versions(esp_app_desc_t* app_descriptor)
{
    ESP_LOG_BUFFER_HEX_LEVEL(__func__, (uint8_t *)app_descriptor, 256, ESP_LOG_ERROR);
    esp_app_desc_t* current_app_descriptor = esp_ota_get_app_description();
    ESP_LOG_BUFFER_HEX_LEVEL(__func__, (uint8_t *)current_app_descriptor, 256, ESP_LOG_ERROR);
    return strcmp((char*)current_app_descriptor->app_elf_sha256, (char*)app_descriptor->app_elf_sha256) == 0;
}
