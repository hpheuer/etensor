# etensor

**E-Tensor** ist ein auf dem **ESP32-P4** basierendes Gerät zur statistischen Analyse von Hardware-Zufallszahlen nach dem Vorbild des [Global Consciousness Projects (GCP)](https://noosphere.princeton.edu/). Es misst, ob mentale Fokussierung einen messbaren Einfluss auf einen echten Hardware-Zufallsgenerator (TRNG) hat.

## Was es macht

Das Gerät liest kontinuierlich Rohwerte aus dem Hardware-TRNG des ESP32-P4 und wertet sie nach der GCP-Methodik aus:

- **Phase 1 – Baseline:** 100 Läufe zur Ermittlung des individuellen Rauschpegels
- **Phase 2 – Messung:** 8.000 Läufe à 50 Segmenten mit je 200 Bits
- **Auswertung:** Z-Score, Chi², Richtung und statistischer p-Wert
- **Ergebnis:** Sofortanzeige im Browser – von *nicht signifikant* bis *p < 0.001 (hoch signifikant!)*

Die Bedienung läuft vollständig über einen integrierten Webserver – kein App, kein Cloud-Dienst.

## Hardware

| Komponente | Details |
|---|---|
| Board | Waveshare ESP32-P4-ETH |
| PHY | IP101GRI via RMII |
| Verbindung | Kabelgebundenes Ethernet |
| Zufallsquelle | Hardware-TRNG (`0x501101A4`) |

## Software-Stack

- **ESP-IDF** (FreeRTOS, ESP-HTTP-Server, SPIFFS, NVS)
- Webserver mit dynamisch gerenderten HTML-Seiten
- SPIFFS-Partition für statische Assets (Icons etc.)
- Eigene Partitionstabelle (`partitions.csv`)

## Projektstruktur

```
etensor/
├── main/
│   ├── etensor.c          # app_main, Systemstart
│   ├── etensor.h          # Typen, Konstanten, Deklarationen
│   ├── sensor.c           # TRNG-Zugriff & GCP-Analyse
│   ├── httpkram.c         # Ethernet, SPIFFS, Webserver, HTTP-Handler
│   └── etensor_test.html  # Web-UI Vorlage
├── spiffs_image/          # Statische Dateien (wird geflasht)
├── partitions.csv         # Partitionstabelle
├── sdkconfig              # ESP-IDF Konfiguration
└── CMakeLists.txt
```

## Build & Flash

Entwicklung mit **VS Code + Espressif ESP-IDF Extension**:

| Aktion | Shortcut |
|---|---|
| Build + Flash + Monitor | `Ctrl+Shift+B` |
| Nur Build | `Ctrl+E B` |
| Nur Flash | `Ctrl+E F` |
| Monitor | `Ctrl+E M` |
| Menuconfig | `Ctrl+E G` |

> **Target:** `esp32p4` – niemals ein anderes Target verwenden!

## Benutzung

1. Gerät per Ethernet verbinden und einschalten
2. IP-Adresse aus dem seriellen Monitor ablesen
3. Browser öffnen → IP-Adresse eingeben
4. Konzentrieren, **▶ Start** drücken, Ergebnis abwarten (~10 Sekunden)
5. **„Eine weitere Frage"** für eine neue Messung
