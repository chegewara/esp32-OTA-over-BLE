#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

extern "C" {
	void app_main(void);
}

extern void OTA_over_BLE(void);


void app_main()
{
    OTA_over_BLE();
}
