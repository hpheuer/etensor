#pragma once
// ==================================================================
//  ha_push.h  –  Home Assistant REST API Integration
// ==================================================================
#include <stdbool.h>
#include <stddef.h>

// Messergebnisse an Home Assistant senden (kein Absturz wenn nicht konfiguriert)
void ha_push_results(double baseline, double zscore, double chisq,
                     const char *direction, const char *pvalue);

// Konfiguration in NVS speichern
// token == NULL oder leer → bestehender Token bleibt erhalten
void ha_config_save(const char *url, const char *token);

// HA-URL für Anzeige laden (leer wenn nicht konfiguriert)
void ha_config_load_url(char *out, size_t len);

// Prüfen ob Token bereits hinterlegt ist
bool ha_token_is_set(void);
