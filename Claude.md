\# etensor – ESP32-P4 Projekt



\## Umgebung

\- Windows, Laufwerk D:\\e-tensor\\etensor

\- ESP-IDF unter C:\\esp\\esp-idf

\- VS Code mit Espressif ESP-IDF Extension

\- Target: \*\*esp32p4\*\* (niemals esp32, esp32s3 o.ä. annehmen!)

\- Build-System: idf.py via ESP-IDF Extension



\## Projektstruktur

\- main/            – Hauptcode

\- spiffs\_image/    – SPIFFS-Dateisystem (wird mit geflasht)

\- build/           – Ausgabeverzeichnis (nicht bearbeiten)

\- CMakeLists.txt   – Build-Konfiguration

\- partitions.csv   – eigene Partitionstabelle

\- sdkconfig        – ESP-IDF Konfiguration



\## Build, Flash, Monitor

Claude ruft diese Befehle NICHT selbst auf.

Der Nutzer startet sie über VS Code:



| Aktion              | VS Code Shortcut | Command Palette                              |

|---------------------|------------------|----------------------------------------------|

| Bauen               | Ctrl+E B         | ESP-IDF: Build your project                  |

| Flashen             | Ctrl+E F         | ESP-IDF: Flash your project                  |

| Monitor             | Ctrl+E M         | ESP-IDF: Monitor your device                 |

| Build+Flash+Monitor | Ctrl+Shift+B     | Task: Build, Flash \& Monitor (tasks.json)    |

| Full Clean          | –                | ESP-IDF: Full Clean                          |

| Menuconfig          | Ctrl+E G         | ESP-IDF: SDK Configuration Editor            |



\## Regeln für Claude

\- sdkconfig NIEMALS manuell bearbeiten – immer Menuconfig verwenden

\- partitions.csv bei Speicheränderungen anpassen

\- Pfade immer Windows-Style: D:\\e-tensor\\etensor\\...

\- Bei Fehlern: Fehlermeldung aus dem ESP-IDF Terminal einfügen

\- spiffs\_image/ Inhalt wird automatisch mit geflasht (SPIFFS-Partition)

\- Bei CMake-Fehlern → "ESP-IDF: Full Clean" → neu bauen

