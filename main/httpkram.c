// ==================================================================
//  httpkram.c  –  Ethernet, SPIFFS, Webserver, HTTP-Handler
// ==================================================================
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_eth.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_spiffs.h"
#include "esp_system.h"
#include "etensor.h"
#include "ha_push.h"
#include "mdns.h"

static const char *TAG = "HTTP";

// ------------------------------------------------------------------
//  Ethernet Pins (Waveshare ESP32-P4-ETH, IP101GRI via RMII)
// ------------------------------------------------------------------
#define ETH_MDC_GPIO        31
#define ETH_MDIO_GPIO       52
#define ETH_PHY_RST_GPIO    51
#define ETH_PHY_ADDR        1
#define ETH_RMII_TX_EN      49
#define ETH_RMII_TXD0       34
#define ETH_RMII_TXD1       35
#define ETH_RMII_RXD0       29
#define ETH_RMII_RXD1       30
#define ETH_RMII_CRS_DV     28
#define ETH_RMII_CLK_IN     50

// ------------------------------------------------------------------
//  Event-Gruppe (Definition hier, extern in etensor.h)
// ------------------------------------------------------------------
EventGroupHandle_t eth_event_group = NULL;

static esp_eth_handle_t eth_handle = NULL;

// ------------------------------------------------------------------
//  IP-Event Handler
// ------------------------------------------------------------------
static void on_ip_event(void *arg, esp_event_base_t event_base,
                        int32_t event_id, void *event_data)
{
    if (event_id == IP_EVENT_ETH_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "IP-Adresse erhalten: " IPSTR, IP2STR(&event->ip_info.ip));
        if (eth_event_group != NULL)
            xEventGroupSetBits(eth_event_group, ETH_GOT_IP_BIT);
    }
}

// ------------------------------------------------------------------
//  Hilfsfunktion: PNG-Datei aus SPIFFS senden
// ------------------------------------------------------------------
static esp_err_t send_png_file(httpd_req_t *req, const char *path)
{
    httpd_resp_set_type(req, "image/png");
    httpd_resp_set_hdr(req, "Cache-Control", "max-age=86400");
    FILE *f = fopen(path, "rb");
    if (!f) {
        ESP_LOGW(TAG, "Datei nicht gefunden: %s", path);
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }
    char buf[512];
    int  n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
        httpd_resp_send_chunk(req, buf, n);
    fclose(f);
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

// ------------------------------------------------------------------
//  Handler: GET /favicon-32x32.png
// ------------------------------------------------------------------
static esp_err_t favicon32_handler(httpd_req_t *req)
{
    return send_png_file(req, "/spiffs/favicon-32x32.png");
}

// ------------------------------------------------------------------
//  Handler: GET /favicon-16x16.png
// ------------------------------------------------------------------
static esp_err_t favicon16_handler(httpd_req_t *req)
{
    return send_png_file(req, "/spiffs/favicon-16x16.png");
}

// ------------------------------------------------------------------
//  Handler: GET /apple-touch-icon.png
// ------------------------------------------------------------------
static esp_err_t apple_icon_handler(httpd_req_t *req)
{
    return send_png_file(req, "/spiffs/apple-touch-icon.png");
}

// ------------------------------------------------------------------
//  HTML Hauptseite
// ------------------------------------------------------------------
static void send_main_page(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");

    // Head + CSS
    httpd_resp_sendstr_chunk(req,
        "<!DOCTYPE html><html><head><meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width, initial-scale=1'>"
        "<link rel='icon' type='image/png' sizes='32x32' href='/favicon-32x32.png'>"
        "<link rel='icon' type='image/png' sizes='16x16' href='/favicon-16x16.png'>"
        "<link rel='apple-touch-icon' sizes='180x180' href='/apple-touch-icon.png'>"
        "<title>E-Tensor</title><style>"
        "body{font-family:monospace;background:#1a1a2e;color:#eee;"
        "max-width:700px;margin:20px auto;padding:15px;}"
        "h1{color:#00d4ff;margin-bottom:4px;font-size:1.4em;}"
        ".sub{color:#888;font-size:0.85em;margin-bottom:20px;}"
        ".btn-run{background:#00ff88;color:#000;border:none;"
        "padding:18px 40px;cursor:pointer;font-size:1.2em;"
        "font-weight:bold;width:100%;box-sizing:border-box;}"
        ".btn-run:hover{background:#00cc66;}"
        ".btn-busy{background:#555;color:#999;border:none;"
        "padding:18px 40px;font-size:1.2em;font-weight:bold;"
        "width:100%;box-sizing:border-box;cursor:not-allowed;}"
        ".btn-start-inline{background:#00ff88;color:#000;padding:2px 10px;"
        "border-radius:4px;font-weight:bold;border:none;cursor:pointer;"
        "font-family:monospace;font-size:1em;}"
        ".btn-start-inline:hover{background:#00cc66;}"
        ".btn-reset{background:#c0392b;color:#fff;border:none;"
        "padding:8px 20px;cursor:pointer;font-size:0.85em;"
        "font-weight:bold;}"
        ".btn-reset:hover{background:#e74c3c;}"
        ".btn-help{background:transparent;color:#888;border:1px solid #555;"
        "padding:8px 20px;cursor:pointer;font-size:0.85em;"
        "font-family:monospace;}"
        ".btn-help:hover{color:#eee;border-color:#888;}"
        ".footer{display:flex;justify-content:space-between;align-items:center;"
        "margin-top:10px;}"
        "pre{background:#0f0f23;border:1px solid #444;padding:14px;"
        "white-space:pre-wrap;color:#ffcc00;"
        "max-height:400px;overflow-y:auto;font-size:1.1em;margin:0;}"
        ".card{background:#0f0f23;border:1px solid #333;padding:15px;"
        "margin-bottom:15px;}"
        ".tbl{border-collapse:collapse;}"
        ".tbl td{padding:2px 0;vertical-align:top;}"
        "</style>"
    );

    // Meta-Refresh nur während laufender Messung
    if (doit_running) {
        httpd_resp_sendstr_chunk(req,
            "<meta http-equiv='refresh' content='2'>");
    }

    httpd_resp_sendstr_chunk(req,
        "</head><body>"
        "<h1>&#9889; E-Tensor</h1>"
        "<div class='sub'>Waveshare ESP32-P4-ETH &bull; IP101GRI &bull; RMII"
        "&nbsp;&nbsp;<a href='/config' style='color:#444;text-decoration:none;"
        "font-size:0.95em;' title='Konfiguration'>&#9881;</a></div>"
    );

    // Start-Button (gesperrt während Messung)
    httpd_resp_sendstr_chunk(req,
        "<div class='card'><form method='post' action='/run'>");
    if (doit_running) {
        httpd_resp_sendstr_chunk(req,
            "<button class='btn-busy' type='submit' disabled>"
            "&#8987; l&auml;uft...</button>");
    } else {
        httpd_resp_sendstr_chunk(req,
            "<button class='btn-run' type='submit'>&#9654; Start</button>");
    }
    httpd_resp_sendstr_chunk(req, "</form></div>");

    // Ausgabefeld oder Anleitung
    httpd_resp_sendstr_chunk(req,
        "<div class='card'>"
        "<div style='background:#0f0f1a;border:1px solid #444;padding:14px;"
        "color:#ffcc00;font-size:1em;line-height:1.9;'>"
    );

    if (output_len > 0) {
        httpd_resp_sendstr_chunk(req, output_buf);
    } else {
        // Anleitung
        httpd_resp_sendstr_chunk(req,
            "1. Ja/Nein-Frage klar definieren<br>"
            "2. Stark <em style='color:#ffcc00;font-style:normal;"
            "font-weight:bold;'>darauf</em> konzentrieren<br>"
            "3. Konzentration <em style='color:#ffcc00;font-style:normal;"
            "font-weight:bold;'>halten</em> und&#160;"
        );
        if (doit_running) {
            httpd_resp_sendstr_chunk(req,
                "<span style='color:#555;'>Start</span>");
        } else {
            httpd_resp_sendstr_chunk(req,
                "<form method='post' action='/run' "
                "style='display:inline;margin:0;padding:0;'>"
                "<button type='submit' style='background:none;border:none;"
                "color:#00ff88;cursor:pointer;font-family:monospace;"
                "font-size:1em;padding:0;text-decoration:underline;'>"
                "Start</button></form>");
        }
        httpd_resp_sendstr_chunk(req,
            "<br>"
            "4. <span style='color:#ff8888;'>"
            "Nicht nachtesten, kein Zweifel!</span>"
        );
    }

    httpd_resp_sendstr_chunk(req, "</div>");

    // Daumen-Symbol außerhalb des <pre>
    if (current_result != RESULT_NONE) {
        httpd_resp_sendstr_chunk(req,
            "<div style='font-size:84px;text-align:center;line-height:1.2;margin-top:20px;'>"
        );
        switch (current_result) {
            case RESULT_POSITIVE:
                httpd_resp_sendstr_chunk(req, "&#128077;");
                break;
            case RESULT_VERY_POSITIVE:
                httpd_resp_sendstr_chunk(req, "&#128077; &#128077;");
                break;
            case RESULT_NEGATIVE:
                httpd_resp_sendstr_chunk(req, "&#128078;");
                break;
            case RESULT_VERY_NEGATIVE:
                httpd_resp_sendstr_chunk(req, "&#128078; &#128078;");
                break;
            default:
                break;
        }
        httpd_resp_sendstr_chunk(req, "</div>");
    }

    // Footer: Weitere Frage (nur sichtbar nach Messung), Hilfe rechts
    httpd_resp_sendstr_chunk(req, "<div class='footer'>");
    if (current_result != RESULT_NONE) {
        httpd_resp_sendstr_chunk(req,
            "<form method='post' action='/reset' style='margin:0;'>"
            "<button class='btn-reset' type='submit'>&#8635; Eine weitere Frage</button>"
            "</form>");
    } else {
        // Platzhalter damit Hilfe-Button rechts bleibt
        httpd_resp_sendstr_chunk(req, "<span></span>");
    }
    httpd_resp_sendstr_chunk(req,
        "<button class='btn-help' type='button' "
        "onclick=\"window.open('http://ichbinwasichbin.de/html/pendeln.html','_blank');\">"
        "&#10067; Hilfe</button>"
        "</div>"
    );

    httpd_resp_sendstr_chunk(req, "</div></body></html>");
    httpd_resp_sendstr_chunk(req, NULL);
}

// ------------------------------------------------------------------
//  Handler: GET /
// ------------------------------------------------------------------
static esp_err_t root_get_handler(httpd_req_t *req)
{
    send_main_page(req);
    return ESP_OK;
}

// ------------------------------------------------------------------
//  Handler: POST /run
// ------------------------------------------------------------------
static esp_err_t run_post_handler(httpd_req_t *req)
{
    if (!doit_running) {
        current_result = RESULT_NONE;
        output_len     = 0;
        memset(output_buf, 0, OUTPUT_BUF_SIZE);
        doit_running   = true;
        output_len     = snprintf(output_buf, OUTPUT_BUF_SIZE, "Messung laeuft...");
        xTaskCreate(doit_task, "doit", 16384, NULL, 5, NULL);
        ESP_LOGI(TAG, "doit_task gestartet");
    } else {
        ESP_LOGW(TAG, "Messung laeuft bereits – Anfrage ignoriert");
    }
    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", "/");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

// ------------------------------------------------------------------
//  Handler: POST /reset  – Antwort senden, dann ESP32 neu starten
// ------------------------------------------------------------------
static esp_err_t reset_post_handler(httpd_req_t *req)
{
    ESP_LOGW(TAG, "Reset angefordert – starte neu...");
    httpd_resp_set_type(req, "text/html");
    httpd_resp_sendstr(req,
        "<!DOCTYPE html><html><head><meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width, initial-scale=1'>"
        "<meta http-equiv='refresh' content='5;url=/'>"
        "<title>Reset</title></head>"
        "<body style='font-family:monospace;background:#1a1a2e;color:#eee;"
        "text-align:center;padding-top:80px;'>"
        "<h2 style='color:#00d4ff;'>&#8635; Neu-Initialisierung...</h2>"
        "<p style='color:#888;'>Seite l&auml;dt in 5 Sekunden neu.</p>"
        "</body></html>"
    );
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
    return ESP_OK; // wird nie erreicht
}

// ------------------------------------------------------------------
//  Hilfsfunktionen: URL-Decode und Formularfeld-Parser
// ------------------------------------------------------------------
static void url_decode(char *dst, const char *src, size_t max_len)
{
    size_t i = 0;
    while (*src && i < max_len - 1) {
        if (*src == '+') {
            dst[i++] = ' '; src++;
        } else if (*src == '%' && src[1] && src[2]) {
            char hex[3] = {src[1], src[2], 0};
            dst[i++] = (char)strtol(hex, NULL, 16);
            src += 3;
        } else {
            dst[i++] = *src++;
        }
    }
    dst[i] = '\0';
}

static void parse_field(const char *body, const char *field,
                        char *out, size_t max_len)
{
    char key[32];
    snprintf(key, sizeof(key), "%s=", field);
    const char *p = strstr(body, key);
    if (!p) { out[0] = '\0'; return; }
    p += strlen(key);
    const char *end = strchr(p, '&');
    size_t len = end ? (size_t)(end - p) : strlen(p);
    if (len >= max_len) len = max_len - 1;
    char encoded[512] = {0};
    if (len >= sizeof(encoded)) len = sizeof(encoded) - 1;
    memcpy(encoded, p, len);
    url_decode(out, encoded, max_len);
}

// ------------------------------------------------------------------
//  Handler: POST /api/start  – Messung per HA oder HTTP auslösen
// ------------------------------------------------------------------
static esp_err_t api_start_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");
    if (doit_running) {
        httpd_resp_sendstr(req, "{\"status\":\"busy\"}");
        return ESP_OK;
    }
    current_result = RESULT_NONE;
    output_len     = 0;
    memset(output_buf, 0, OUTPUT_BUF_SIZE);
    doit_running   = true;
    output_len     = snprintf(output_buf, OUTPUT_BUF_SIZE, "Messung laeuft...");

    // Status sofort an HA melden (kurzer Timeout)
    ha_push_status("running");

    xTaskCreate(doit_task, "doit", 16384, NULL, 5, NULL);
    ESP_LOGI(TAG, "doit_task gestartet via /api/start");
    httpd_resp_sendstr(req, "{\"status\":\"started\"}");
    return ESP_OK;
}

// ------------------------------------------------------------------
//  Handler: GET /config
// ------------------------------------------------------------------
static esp_err_t config_get_handler(httpd_req_t *req)
{
    char cur_url[256] = {0};
    ha_config_load_url(cur_url, sizeof(cur_url));
    bool token_set = ha_token_is_set();

    httpd_resp_set_type(req, "text/html");
    httpd_resp_sendstr_chunk(req,
        "<!DOCTYPE html><html><head><meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width, initial-scale=1'>"
        "<title>E-Tensor Konfiguration</title><style>"
        "body{font-family:monospace;background:#1a1a2e;color:#eee;"
        "max-width:700px;margin:20px auto;padding:15px;}"
        "h1{color:#00d4ff;margin-bottom:4px;font-size:1.4em;}"
        ".sub{color:#888;font-size:0.85em;margin-bottom:20px;}"
        ".card{background:#0f0f23;border:1px solid #333;padding:15px;"
        "margin-bottom:15px;}"
        "label{color:#888;font-size:0.85em;display:block;margin-bottom:4px;}"
        "input,textarea{width:100%;box-sizing:border-box;background:#2a2a3e;"
        "color:#eee;border:2px solid #555;padding:10px;"
        "font-family:monospace;font-size:0.9em;margin-bottom:14px;"
        "caret-color:#eee;outline:none;border-radius:3px;}"
        "input:focus,textarea:focus{border-color:#00d4ff;}"
        "textarea{resize:vertical;height:80px;}"
        ".btn{background:#00ff88;color:#000;border:none;padding:10px 24px;"
        "cursor:pointer;font-size:1em;font-weight:bold;width:100%;}"
        ".btn:hover{background:#00cc66;}"
        ".note{color:#555;font-size:0.8em;margin-top:-10px;margin-bottom:14px;}"
        ".back{color:#555;font-size:0.85em;text-decoration:none;"
        "display:block;text-align:center;margin-top:14px;}"
        ".back:hover{color:#888;}"
        ".ok{color:#00ff88;} .warn{color:#ffcc00;}"
        "</style></head><body>"
        "<h1>&#9881; Konfiguration</h1>"
        "<div class='sub'>Home Assistant Integration</div>"
        "<div class='card'>"
        "<form method='post' action='/config'>"
    );

    // URL-Feld mit aktuellem Wert (als ein Chunk – leerer Chunk würde HTTP beenden!)
    {
        char input_buf[512];
        snprintf(input_buf, sizeof(input_buf),
            "<label>Home Assistant URL</label>"
            "<input type='text' name='ha_url' "
            "placeholder='http://192.168.1.100:8123' value='%s'>",
            cur_url);
        httpd_resp_sendstr_chunk(req, input_buf);
    }

    // Token-Feld – nie den echten Token anzeigen
    httpd_resp_sendstr_chunk(req, "<label>Long-Lived Access Token</label>");
    httpd_resp_sendstr_chunk(req,
        "<textarea name='ha_token' "
        "placeholder='eyJ0eXAiOiJKV1Qi...'></textarea>");
    if (token_set) {
        httpd_resp_sendstr_chunk(req,
            "<div class='note ok'>&#10003; Token bereits hinterlegt "
            "– leer lassen um ihn beizubehalten</div>");
    } else {
        httpd_resp_sendstr_chunk(req,
            "<div class='note warn'>&#9888; Noch kein Token gespeichert</div>");
    }

    httpd_resp_sendstr_chunk(req,
        "<button class='btn' type='submit'>&#10003; Speichern</button>"
        "</form></div>"
        "<a class='back' href='/'>&#8592; zur&uuml;ck</a>"
        "</body></html>"
    );
    httpd_resp_sendstr_chunk(req, NULL);
    return ESP_OK;
}

// ------------------------------------------------------------------
//  Handler: POST /config
// ------------------------------------------------------------------
static esp_err_t config_post_handler(httpd_req_t *req)
{
    // body auf dem Heap – nicht im HTTP-Task-Stack (nur 4 KB)
    char *body = calloc(1, 900);
    if (!body) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OOM");
        return ESP_FAIL;
    }
    int len = req->content_len;
    if (len <= 0 || len >= 899) len = 899;
    httpd_req_recv(req, body, len);

    char url[256]   = {0};
    char token[512] = {0};
    parse_field(body, "ha_url",   url,   sizeof(url));
    parse_field(body, "ha_token", token, sizeof(token));
    free(body);

    ha_config_save(url, token);
    ESP_LOGI(TAG, "HA Konfiguration: URL=%s token=%s",
             url, strlen(token) > 0 ? "gesetzt" : "unveraendert");

    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", "/config");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

// ------------------------------------------------------------------
//  Webserver starten
// ------------------------------------------------------------------
void start_webserver(void)
{
    httpd_config_t config    = HTTPD_DEFAULT_CONFIG();
    config.recv_wait_timeout = 30;
    config.send_wait_timeout = 30;
    config.max_uri_handlers  = 12;

    httpd_handle_t server = NULL;
    if (httpd_start(&server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "Webserver-Start fehlgeschlagen");
        return;
    }

    static const httpd_uri_t uris[] = {
        { .uri="/",                    .method=HTTP_GET,  .handler=root_get_handler   },
        { .uri="/run",                 .method=HTTP_POST, .handler=run_post_handler    },
        { .uri="/reset",               .method=HTTP_POST, .handler=reset_post_handler  },
        { .uri="/favicon-32x32.png",   .method=HTTP_GET,  .handler=favicon32_handler   },
        { .uri="/favicon-16x16.png",   .method=HTTP_GET,  .handler=favicon16_handler   },
        { .uri="/apple-touch-icon.png",.method=HTTP_GET,  .handler=apple_icon_handler  },
        { .uri="/config",              .method=HTTP_GET,  .handler=config_get_handler  },
        { .uri="/config",              .method=HTTP_POST, .handler=config_post_handler },
        { .uri="/api/start",           .method=HTTP_POST, .handler=api_start_handler   },
    };
    for (int i = 0; i < 9; i++)
        httpd_register_uri_handler(server, &uris[i]);

    // mDNS: Gerät als "etensor.local" im Netzwerk bekannt machen
    mdns_init();
    mdns_hostname_set("etensor");
    mdns_instance_name_set("E-Tensor GCP Device");
    mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0);
    ESP_LOGI(TAG, "mDNS: erreichbar als http://etensor.local");

    // Initialstatus an HA melden
    ha_push_status("idle");

    ESP_LOGI(TAG, "Webserver laeuft auf Port 80");
}

// ------------------------------------------------------------------
//  Webserver Task (wartet auf IP, dann Start)
// ------------------------------------------------------------------
static void webserver_task(void *pvParameters)
{
    EventBits_t bits = xEventGroupWaitBits(eth_event_group, ETH_GOT_IP_BIT,
                                           pdFALSE, pdTRUE,
                                           pdMS_TO_TICKS(30000));
    if (!(bits & ETH_GOT_IP_BIT)) {
        ESP_LOGE(TAG, "Kein Ethernet nach 30s – Webserver-Start abgebrochen");
        vTaskDelete(NULL);
        return;
    }
    start_webserver();
    vTaskDelete(NULL);
}

// ------------------------------------------------------------------
//  SPIFFS Init
// ------------------------------------------------------------------
static void spiffs_init(void)
{
    esp_vfs_spiffs_conf_t conf = {
        .base_path              = "/spiffs",
        .partition_label        = NULL,
        .max_files              = 8,
        .format_if_mount_failed = true
    };
    ESP_ERROR_CHECK(esp_vfs_spiffs_register(&conf));
    size_t total = 0, used = 0;
    esp_spiffs_info(NULL, &total, &used);
    ESP_LOGI(TAG, "SPIFFS: %d/%d Bytes genutzt", used, total);
}

// ------------------------------------------------------------------
//  Ethernet Init
// ------------------------------------------------------------------
void ethernet_init(void)
{
    ESP_LOGI(TAG, "Starte Ethernet-Initialisierung...");

    esp_err_t event_err = esp_event_loop_create_default();
    if (event_err != ESP_OK && event_err != ESP_ERR_INVALID_STATE)
        ESP_LOGE(TAG, "Event Loop Fehler: 0x%X", event_err);

    esp_netif_config_t cfg = ESP_NETIF_DEFAULT_ETH();
    esp_netif_t *eth_netif = esp_netif_new(&cfg);
    if (!eth_netif) {
        ESP_LOGE(TAG, "esp_netif_new fehlgeschlagen!");
        return;
    }

    eth_mac_config_t        mac_config  = ETH_MAC_DEFAULT_CONFIG();
    eth_esp32_emac_config_t emac_config = ETH_ESP32_EMAC_DEFAULT_CONFIG();
    emac_config.smi_gpio.mdc_num        = ETH_MDC_GPIO;
    emac_config.smi_gpio.mdio_num       = ETH_MDIO_GPIO;
    emac_config.interface               = EMAC_DATA_INTERFACE_RMII;
    emac_config.clock_config.rmii.clock_mode = EMAC_CLK_EXT_IN;

    esp_eth_mac_t *mac = esp_eth_mac_new_esp32(&emac_config, &mac_config);
    if (!mac) {
        ESP_LOGE(TAG, "esp_eth_mac_new_esp32 fehlgeschlagen!");
        return;
    }

    eth_phy_config_t phy_config  = ETH_PHY_DEFAULT_CONFIG();
    phy_config.phy_addr          = ETH_PHY_ADDR;
    phy_config.reset_gpio_num    = ETH_PHY_RST_GPIO;

    esp_eth_phy_t *phy = esp_eth_phy_new_generic(&phy_config);
    if (!phy) {
        ESP_LOGE(TAG, "PHY-Erstellung fehlgeschlagen!");
        return;
    }

    esp_eth_config_t eth_config = ETH_DEFAULT_CONFIG(mac, phy);
    esp_err_t ret = esp_eth_driver_install(&eth_config, &eth_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_eth_driver_install fehlgeschlagen! 0x%X (%s)",
                 ret, esp_err_to_name(ret));
        return;
    }

    void *glue = esp_eth_new_netif_glue(eth_handle);
    if (!glue) {
        ESP_LOGE(TAG, "esp_eth_new_netif_glue fehlgeschlagen!");
        return;
    }
    esp_netif_attach(eth_netif, glue);

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_ETH_GOT_IP, &on_ip_event, NULL, NULL));

    esp_eth_start(eth_handle);
    ESP_LOGI(TAG, "Ethernet erfolgreich gestartet!");

    // SPIFFS und Webserver-Task starten
    spiffs_init();
    xTaskCreate(webserver_task, "webserver", 16384, NULL, 5, NULL);
}
