// ==================================================================
//  sensor.c  –  TRNG-Zugriff, GCP-Analyse, Messung
// ==================================================================
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <sys/times.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_task_wdt.h"

#include "etensor.h"
#include "ha_push.h"

// Stub: verhindert Linker-Fehler wegen fehlender Implementierung
clock_t times(struct tms *buf) {
    return (clock_t)-1;
}

static const char *TAG = "SENSOR";

// ------------------------------------------------------------------
//  Gemeinsame Variablen (Definitionen – Deklarationen in etensor.h)
// ------------------------------------------------------------------
char                    output_buf[OUTPUT_BUF_SIZE];  // für Ausgabefeld
int                     output_len  = 0;
volatile result_status_t current_result = RESULT_NONE;
volatile bool           doit_running    = false;

// ------------------------------------------------------------------
//  TRNG-Register des ESP32-P4
// ------------------------------------------------------------------
#define ESP32P4_RNG_DATA_REG  0x501101A4L

static inline uint32_t trng_read_raw(void)
{
    return *((volatile uint32_t *)ESP32P4_RNG_DATA_REG);
}

// ------------------------------------------------------------------
//  GCP-Analyse Konstanten
// ------------------------------------------------------------------
#define GCP_SEGMENT_BITS    200     // GCP-Standard: 200 Bit pro Segment
#define GCP_SEGMENTS         50     // Anzahl Segmente pro Lauf
#define GCP_RUNS           8000     // Gesamtläufe pro Messung
#define GCP_BASELINE_RUNS   100     // erste Läufe als Baseline
#define BUFFER_SIZE        2560     // uint32_t – reicht für alle GCP-Varianten

static uint32_t *random_buffer = NULL;

// ------------------------------------------------------------------
//  Buffer-Initialisierung (einmalig in app_main aufrufen)
// ------------------------------------------------------------------
void init_buffer(void)
{
    random_buffer = (uint32_t *)malloc(BUFFER_SIZE * sizeof(uint32_t));
    if (random_buffer == NULL) {
        ESP_LOGE(TAG, "Konnte keinen Speicher fuer TRNG-Buffer allokieren!");
    }
}

// ------------------------------------------------------------------
//  Ein Segment auswerten: Z-Score aus 200 Bits
//  Erwartungswert: 100 Einsen, Varianz: 50, StdDev: sqrt(50)
// ------------------------------------------------------------------
static double segment_zscore(uint32_t *words, int start_word, int bit_offset)
{
    int ones        = 0;
    int bits_counted = 0;
    int w = start_word;
    int b = bit_offset;

    while (bits_counted < GCP_SEGMENT_BITS) {
        uint32_t val = words[w];
        while (b < 32 && bits_counted < GCP_SEGMENT_BITS) {
            ones += (val >> b) & 1;
            b++;
            bits_counted++;
        }
        b = 0;
        w++;
    }
    return (ones - 100.0) / 7.07106781; // sqrt(50)
}

// ------------------------------------------------------------------
//  Einen Lauf durchführen: gibt normierten Z-Score zurück
// ------------------------------------------------------------------
static double run_one(void)
{
    int words_needed = (GCP_SEGMENTS * GCP_SEGMENT_BITS + 31) / 32 + 8;
    if (words_needed > BUFFER_SIZE) words_needed = BUFFER_SIZE;

    for (int i = 0; i < words_needed; i++)
        random_buffer[i] = trng_read_raw();

    double run_z_sum = 0.0;
    int w = 0, b = 0;

    for (int seg = 0; seg < GCP_SEGMENTS; seg++) {
        run_z_sum += segment_zscore(random_buffer, w, b);
        b += GCP_SEGMENT_BITS;
        w += b / 32;
        b  = b % 32;
    }
    return run_z_sum / sqrt(GCP_SEGMENTS);
}

// ------------------------------------------------------------------
//  Doit(): GCP-Analyse mit Baseline-Korrektur
//
//  Phase 1 (Läufe 1..GCP_BASELINE_RUNS):  Baseline ermitteln
//  Phase 2 (Läufe GCP_BASELINE_RUNS+1..GCP_RUNS): Messung,
//           relativ zur Baseline ausgewertet
//  Fortschritt: alle DOT_DISTANCE Läufe ein '.' im output_buf
// ------------------------------------------------------------------
void Doit(void)
{
    memset(output_buf, 0, OUTPUT_BUF_SIZE);

    if (random_buffer == NULL) {
        output_len = snprintf(output_buf, OUTPUT_BUF_SIZE,
                              "******* FEHLER: Kein Speicher! *******\n");
        return;
    }

    output_len = snprintf(output_buf, OUTPUT_BUF_SIZE, "Messung l\xc3\xa4uft...");

    // ── Phase 1: Baseline ────────────────────────────────────────
    double baseline_z_sum = 0.0;
    ESP_LOGI(TAG, "Baseline laeuft (%d Laeufe)...", GCP_BASELINE_RUNS);

    for (int run = 0; run < GCP_BASELINE_RUNS; run++) {
        baseline_z_sum += run_one();
    }
    double baseline_mean = baseline_z_sum / GCP_BASELINE_RUNS;

    // ── Phase 2: Messung ─────────────────────────────────────────
    ESP_LOGI(TAG, "Messung l\xc3\xa4uft (%d Laeufe)...", GCP_RUNS);

    double meas_z_sum = 0.0;
    for (int run = 0; run < GCP_RUNS; run++) {
        meas_z_sum += run_one() - baseline_mean;   // Baseline-korrigiert
    }

    // ── Gesamtauswertung ─────────────────────────────────────────
    double total_z     = meas_z_sum / sqrt(GCP_RUNS);
    double total_chisq = total_z * total_z;
    double absZ        = fabs(total_z);

    // p-Wert ohne *** Präfix – passt besser auf Handy-Display
    const char *significance = "nicht signifikant";
    if      (absZ > 3.29) significance = "p < 0.001 (hoch signifikant!)";
    else if (absZ > 2.58) significance = "p < 0.01";
    else if (absZ > 1.96) significance = "p < 0.05";
    else if (absZ > 1.28) significance = "p < 0.10 (Trend)";

    const char *direction = (total_z >= 0.0) ? "positiv" : "negativ";

    // Kurze Labels – Tabellen-Layout für saubere Doppelpunkt-Ausrichtung
    int remaining = OUTPUT_BUF_SIZE - 1;
    int written   = snprintf(output_buf, remaining,
        "<table class='tbl'>"
        "<tr><td>Baseline</td><td>&nbsp;:&nbsp;</td><td>%+.4f</td></tr>"
        "<tr><td>L&auml;ufe</td><td>&nbsp;:&nbsp;</td><td>%d</td></tr>"
        "<tr><td>Z-Score</td><td>&nbsp;:&nbsp;</td><td>%+.4f</td></tr>"
        "<tr><td>Chi&sup2;</td><td>&nbsp;:&nbsp;</td><td>%.4f</td></tr>"
        "<tr><td>Richtung</td><td>&nbsp;:&nbsp;</td><td>%s</td></tr>"
        "<tr><td>p-Wert</td><td>&nbsp;:&nbsp;</td><td>%s</td></tr>"
        "</table>",
        baseline_mean, GCP_RUNS, total_z, total_chisq, direction, significance);

    if (written > 0) output_len = written;

    if (total_z >= 0.0) {
        if  (absZ > 3.29)
            current_result = RESULT_VERY_POSITIVE;
        else
            current_result = RESULT_POSITIVE;

        
    }
    else {
        if  (absZ > 3.29)
          current_result = RESULT_VERY_NEGATIVE;
        else
          current_result = RESULT_NEGATIVE;
    } 
    
    ESP_LOGI(TAG, "GCP fertig: Baseline=%.4f Z=%.4f ChiSq=%.4f",
             baseline_mean, total_z, total_chisq);

    // Ergebnisse an Home Assistant senden (nur wenn konfiguriert)
    ha_push_results(baseline_mean, total_z, total_chisq, direction, significance);
}

// ------------------------------------------------------------------
//  doit_task: Doit() als FreeRTOS-Task, damit HTTP nicht blockiert
// ------------------------------------------------------------------
void doit_task(void *pvParameters)
{
    // Watchdog großzügig – Doit() läuft komplett ohne Unterbrechung durch
    esp_task_wdt_config_t wdt_cfg = {
        .timeout_ms    = 120000,    // 2 Minuten – sicher auch bei langsamem TRNG
        .idle_core_mask = 0
    };
    esp_task_wdt_reconfigure(&wdt_cfg);

    Doit();

    doit_running = false;
    vTaskDelete(NULL);
}
