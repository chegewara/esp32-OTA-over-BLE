/* ESP HTTP Client Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_ota_ops.h"

#include "esp_http_client.h"
#include "http_client.h"

#define MAX_HTTP_RECV_BUFFER 512
#define BINARY_FILE_URL "http://192.168.0.5/hello-world.bin"

static const char *TAG = "HTTP_CLIENT";

static uint32_t total_len = 0;
static uint8_t* response;
static ota_file_t* ota_file;

esp_err_t _http_event_handler(esp_http_client_event_t *evt)
{
	switch (evt->event_id) {
        case HTTP_EVENT_ERROR:
            ESP_LOGD(TAG, "HTTP_EVENT_ERROR");
            break;
        case HTTP_EVENT_ON_CONNECTED:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_CONNECTED");
            break;
        case HTTP_EVENT_HEADER_SENT:
            ESP_LOGD(TAG, "HTTP_EVENT_HEADER_SENT");
            break;
        case HTTP_EVENT_ON_HEADER:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
            break;
        case HTTP_EVENT_ON_DATA:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
            
            if (!esp_http_client_is_chunked_response(evt->client)) {
                // Write out data
                memcpy(response + total_len, (uint8_t*)evt->data, evt->data_len);
                total_len += evt->data_len;
                ESP_LOGI(TAG, "%d ---> %d", evt->data_len, total_len);
            }

            break;
        case HTTP_EVENT_ON_FINISH:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_FINISH");
            break;
        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGD(TAG, "HTTP_EVENT_DISCONNECTED");
            break;
        default:
            ESP_LOGI(TAG, "%d", evt->event_id);
            break;
	}
	return ESP_OK;
}

void http_client(void* data) 
{
	total_len = 0;
    // heap_caps_print_heap_info(MALLOC_CAP_INTERNAL);
	response = (uint8_t*) heap_caps_calloc(1024*2000, 1, MALLOC_CAP_SPIRAM);
    ota_file = (ota_file_t*)calloc(1, sizeof(ota_file_t));
    heap_caps_print_heap_info(MALLOC_CAP_SPIRAM);


	char request[120]; 
	sprintf(request, "%s", BINARY_FILE_URL);
	esp_http_client_config_t config = {
		.url = request,
		.event_handler = _http_event_handler
	};

	esp_http_client_handle_t client = esp_http_client_init(&config);
	esp_http_client_set_method(client, HTTP_METHOD_GET);
	esp_err_t err = esp_http_client_perform(client);
	if (err == ESP_OK) {
		ESP_LOGI(TAG, "HTTP GET Status = %d, content_length = %d",
			esp_http_client_get_status_code(client),
			esp_http_client_get_content_length(client));
	}
	else {
		ESP_LOGE(TAG, "HTTP GET request failed: %s", esp_err_to_name(err));
	}

	ESP_LOGI(TAG , "stack watermark: %d", uxTaskGetStackHighWaterMark(NULL));

    ota_file->response = response;
    ota_file->size = total_len;
    ESP_LOGE(TAG, "total size: %d", total_len);

	esp_http_client_cleanup(client);
	vTaskDelete(NULL);
}

uint8_t* get_ota_version()
{
    esp_app_desc_t* new_app_info = (response + 32);    // sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t) = 32

    ESP_LOGW(TAG, "Project name:     %s", new_app_info->project_name);
    ESP_LOGW(TAG, "App version:      %s", new_app_info->version);
    ESP_LOGW(TAG, "Compile time:     %s %s", new_app_info->date, new_app_info->time);
    ESP_LOGW(TAG, "ELF file SHA256:  %02x%02x%02x%02x%02x%02x%02x%02x...", 
            new_app_info->app_elf_sha256[0], new_app_info->app_elf_sha256[1],
            new_app_info->app_elf_sha256[2], new_app_info->app_elf_sha256[3],
            new_app_info->app_elf_sha256[4], new_app_info->app_elf_sha256[5],
            new_app_info->app_elf_sha256[6], new_app_info->app_elf_sha256[7]
    );

    ESP_LOGW(TAG, "ESP-IDF:          %s", new_app_info->idf_ver);
    return (uint8_t*)new_app_info;
}

ota_file_t* get_ota_file()
{
    return ota_file;
}
