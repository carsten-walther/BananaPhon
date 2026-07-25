#pragma once

#include <Arduino.h>

#include <functional>

#include "Config.h"

// ------------------------------------------------
// OTA (Over-the-Air-Firmware-Update)
// ------------------------------------------------
//
// Bündelt zwei Update-Wege (siehe Config.h):
//   1. ArduinoOTA  — Push aus PlatformIO (espota), Discovery per mDNS.
//   2. Web-Upload  — .bin aus dem Browser hochladen (Core-WebServer +
//                    Update.h, keine zusätzliche Bibliothek).
//
// Beide brauchen eine bestehende WLAN-Verbindung. Wie tryStartRTP() im
// MIDI-Teil starten die Dienste deshalb faul: begin() merkt sich nur die
// Konfiguration, update() bringt sie hoch, sobald WiFi verbunden ist.
class OtaController
{
public:
    using VoidFn     = std::function<void()>;
    using ProgressFn = std::function<void(uint8_t percent)>;

    // Callbacks für Ton/Anzeige — optional. Sie laufen im Kontext des
    // (blockierenden) Updates, nicht aus loop(): handle() bzw.
    // handleClient() kehren erst nach dem Update zurück, der
    // Fortschritt lässt sich also nur von hier aus anzeigen.
    void onStart(const VoidFn& fn)
    {
        _onStart = fn;
    }

    void onProgress(const ProgressFn& fn)
    {
        _onProgress = fn;
    }

    void onEnd(const VoidFn& fn)
    {
        _onEnd = fn;
    }

    void onError(const VoidFn& fn)
    {
        _onError = fn;
    }

    // Konfiguriert die OTA-Dienste (Callbacks, Passwort). Startet sie
    // noch nicht — das übernimmt update() bei bestehender WLAN-Verbindung.
    void begin();

    // Aus loop() aufrufen: startet die Dienste bei WLAN-Verbindung und
    // bedient danach beide OTA-Wege.
    void update();

    // Laufen die OTA-Dienste bereits (WLAN war verbunden)?
    bool active() const
    {
        return _started;
    }

    // Von den (statischen) Handlern beider Wege genutzt — sie teilen
    // sich denselben Anzeige- und Fortschrittscode.
    void beginUpdateUi(); // Fortschritt zurücksetzen + onStart
    void endUpdateUi();   // onEnd (kurz vor dem Neustart)
    void errorUpdateUi(); // onError
    void reportProgress(uint32_t done, uint32_t total);
    void handleWebUpload(); // Datei-Chunks in die Update-Partition
    void handleWebPost();   // Abschluss des POST /update

private:
    void startServices();

    VoidFn _onStart, _onEnd, _onError;
    ProgressFn _onProgress;

    bool _started           = false;
    uint32_t _contentLength = 0;
    int16_t _lastPercent    = -1;
};
