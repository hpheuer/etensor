// ==================================================================
//  etensor.c  –  app_main, Systemstart
// ==================================================================
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "nvs_flash.h"

#include "etensor.h"

static const char *TAG = "ETENSOR";

void app_main(void)
{
    // NVS initialisieren
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "=== E-Tensor v2.0 ===");

    // TRNG-Buffer allokieren
    init_buffer();

    // TCP/IP-Stack starten
    ESP_ERROR_CHECK(esp_netif_init());

    // Event-Gruppe für Ethernet-IP-Ereignis
    eth_event_group = xEventGroupCreate();
    if (eth_event_group == NULL) {
        ESP_LOGE(TAG, "Konnte eth_event_group nicht erstellen!");
        return;
    }

    // Ethernet + SPIFFS + Webserver starten (alles in ethernet_init)
    ethernet_init();

    // Hauptloop: Heap-Überwachung
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(20000));
        ESP_LOGI(TAG, "Heap frei: %lu Bytes",
                 (unsigned long)esp_get_free_heap_size());
    }
}
