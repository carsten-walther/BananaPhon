#pragma once

// Vorlage für die WLAN-Zugangsdaten:
// Diese Datei nach include/Credentials.h kopieren und ausfüllen.
// Credentials.h ist per .gitignore vom Repository ausgeschlossen.

// Leere SSID ("") = keine einkompilierten Zugangsdaten: das Gerät
// nutzt die im Flash gespeicherten Daten aus dem Setup-Portal.
// Eine hier eingetragene SSID hat Vorrang vor dem Portal.
constexpr char WIFI_SSID[]     = "";
constexpr char WIFI_PASSWORD[] = "";
