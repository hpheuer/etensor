// ==================================================================
//  ha_push.c  –  Home Assistant REST API Integration
// ==================================================================
#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include "esp_log.h"
#include "esp_http_client.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "ha_push.h"

static const char *TAG = "HA_PUSH";

#define NVS_NAMESPACE   "etensor_ha"
#define NVS_KEY_URL     "ha_url"
#define NVS_KEY_TOKEN   "ha_tok"

#define MAX_URL_LEN     256
#define MAX_TOKEN_LEN   512

// ------------------------------------------------------------------
//  Konfiguration in NVS speichern
// ------------------------------------------------------------------
void ha_config_save(const char *url, const char *token)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) {
        ESP_LOGE(TAG, "NVS open fehlgeschlagen");
        return;
    }
    if (url && strlen(url) > 0)
        nvs_set_str(h, NVS_KEY_URL, url);
    if (token && strlen(token) > 0)
        nvs_set_str(h, NVS_KEY_TOKEN, token);
    nvs_commit(h);
    nvs_close(h);
    ESP_LOGI(TAG, "HA-Konfiguration gespeichert");
}

// ------------------------------------------------------------------
//  Konfiguration aus NVS laden (intern)
// ------------------------------------------------------------------
static bool ha_config_load(char *url, size_t url_len,
                            char *token, size_t token_len)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK) return false;

    size_t u = url_len, t = token_len;
    bool ok = (nvs_get_str(h, NVS_KEY_URL,   url,   &u) == ESP_OK) &&
              (nvs_get_str(h, NVS_KEY_TOKEN,  token, &t) == ESP_OK) &&
              strlen(url) > 0 && strlen(token) > 0;
    nvs_close(h);
    return ok;
}

// ------------------------------------------------------------------
//  URL für Anzeige im Konfigurationsformular
// ------------------------------------------------------------------
void ha_config_load_url(char *out, size_t len)
{
    nvs_handle_t h;
    out[0] = '\0';
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK) return;
    size_t l = len;
    nvs_get_str(h, NVS_KEY_URL, out, &l);
    nvs_close(h);
}

// ------------------------------------------------------------------
//  Prüfen ob Token hinterlegt ist
// ------------------------------------------------------------------
bool ha_token_is_set(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK) return false;
    // NULL-Buffer: ESP-IDF gibt nur die benötigte Länge zurück, ohne zu lesen
    size_t required = 0;
    esp_err_t err = nvs_get_str(h, NVS_KEY_TOKEN, NULL, &required);
    nvs_close(h);
    return (err == ESP_OK) && (required > 1); // >1 weil Länge den Null-Terminator enthält
}

// ------------------------------------------------------------------
//  Einzelnen Sensor-State an HA REST API senden
// ------------------------------------------------------------------
static void ha_push_one(const char *ha_url, const char *token,
                        const char *entity_id, const char *state,
                        const char *friendly_name, const char *unit,
                        const char *icon)
{
    char url[MAX_URL_LEN + 64];
    snprintf(url, sizeof(url), "%s/api/states/%s", ha_url, entity_id);

    char auth[MAX_TOKEN_LEN + 8];
    snprintf(auth, sizeof(auth), "Bearer %s", token);

    char body[512];
    if (unit && strlen(unit) > 0) {
        snprintf(body, sizeof(body),
            "{\"state\":\"%s\",\"attributes\":{"
            "\"friendly_name\":\"%s\","
            "\"unit_of_measurement\":\"%s\","
            "\"icon\":\"%s\"}}",
            state, friendly_name, unit, icon ? icon : "mdi:gauge");
    } else {
        snprintf(body, sizeof(body),
            "{\"state\":\"%s\",\"attributes\":{"
            "\"friendly_name\":\"%s\","
            "\"icon\":\"%s\"}}",
            state, friendly_name, icon ? icon : "mdi:gauge");
    }

    esp_http_client_config_t cfg = {
        .url         = url,
        .method      = HTTP_METHOD_POST,
        .timeout_ms  = 5000,
        .buffer_size = 512,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    esp_http_client_set_header(client, "Authorization",  auth);
    esp_http_client_set_header(client, "Content-Type",   "application/json");
    esp_http_client_set_post_field(client, body, strlen(body));

    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "HA push OK: %s = %s (HTTP %d)",
                 entity_id, state,
                 esp_http_client_get_status_code(client));
    } else {
        ESP_LOGW(TAG, "HA push fehlgeschlagen (%s): %s",
                 entity_id, esp_err_to_name(err));
    }
    esp_http_client_cleanup(client);
}

// ------------------------------------------------------------------
//  Alle Messergebnisse an HA senden
// ------------------------------------------------------------------
void ha_push_results(double baseline, double zscore, double chisq,
                     const char *direction, const char *pvalue)
{
    char url[MAX_URL_LEN];
    char token[MAX_TOKEN_LEN];

    if (!ha_config_load(url, sizeof(url), token, sizeof(token))) {
        ESP_LOGI(TAG, "HA nicht konfiguriert – kein Push");
        return;
    }

    char val[64];

    snprintf(val, sizeof(val), "%+.4f", zscore);
    ha_push_one(url, token, "sensor.etensor_zscore", val,
                "ETensor Z-Score", "\xcf\x83",
                "mdi:chart-bell-curve-cumulative");

    snprintf(val, sizeof(val), "%.4f", chisq);
    ha_push_one(url, token, "sensor.etensor_chisq", val,
                "ETensor Chi\xc2\xb2", "",
                "mdi:chart-scatter-plot");

    ha_push_one(url, token, "sensor.etensor_direction", direction,
                "ETensor Richtung", "",
                "mdi:compass");

    ha_push_one(url, token, "sensor.etensor_pvalue", pvalue,
                "ETensor p-Wert", "",
                "mdi:sigma");

    snprintf(val, sizeof(val), "%+.4f", baseline);
    ha_push_one(url, token, "sensor.etensor_baseline", val,
                "ETensor Baseline", "",
                "mdi:chart-line");

    ESP_LOGI(TAG, "HA Push abgeschlossen");
}
