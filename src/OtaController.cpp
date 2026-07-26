#include "OtaController.h"

#include <cstring>

#include <WiFi.h>
#include <ESPmDNS.h>
#include <ArduinoOTA.h>
#include <WebServer.h>
#include <Update.h>

// Genau eine Instanz — die statischen Handler greifen über self darauf
// zu (wie RTPMIDIConnection::_instance im MIDI-Teil).
static OtaController* self = nullptr;

static WebServer otaServer(OTA_WEB_PORT);

// Header, den der WebServer normalerweise verwirft — wir brauchen ihn
// für die Prozentanzeige beim Upload (Bytes bisher / Gesamtgröße).
static const char* OTA_HEADERS[] = {"Content-Length"};

// Puffer für den Loop-Austausch (Serialisierung: 10 Byte Kopf + 9 Byte
// je Event; siehe LooperController). Wird für Download und Upload genutzt.
static uint8_t loopBuf[16 + LOOP_MAX_EVENTS * 9];

// Upload-Seite: dunkel, bananengelber Akzent, deutsch. Der Fortschritt
// kommt clientseitig aus XMLHttpRequest.upload — so bleibt die Seite
// beim Neustart des Geräts nicht mit einem Verbindungsfehler stehen.
static const char OTA_PAGE[] PROGMEM = R"HTML(<!doctype html><html lang=de><head>
<meta charset=utf-8><meta name=viewport content="width=device-width,initial-scale=1">
<title>BananaPhon</title><style>
body{font-family:sans-serif;background:#111;color:#eee;margin:0;padding:2rem;text-align:center}
h1{color:#ffd400;margin:.2rem}h2{color:#ffd400;font-size:1.1rem;margin-top:2.5rem}p{color:#bbb}
a{color:#ffd400}
form{margin:1rem 0}input[type=file]{color:#eee;margin:1rem 0}
button{background:#ffd400;border:0;padding:.7rem 1.4rem;border-radius:6px;font-size:1rem;cursor:pointer}
progress{width:100%;height:1.2rem;margin-top:1rem}
.hint{color:#777;font-size:.8rem;margin-top:.5rem}#s,#ls{margin-top:.6rem;color:#ffd400}
</style></head><body>
<h1>BananaPhon</h1>
<h2>Firmware-Update</h2>
<form id=f method=POST action=/update enctype=multipart/form-data>
<input type=file name=firmware accept=.bin required><br>
<button type=submit>Hochladen &amp; Flashen</button></form>
<progress id=p value=0 max=100 hidden></progress><div id=s></div>
<p class=hint>Datei: .pio/build/&lt;env&gt;/firmware.bin — das Gerät startet nach dem Update neu.</p>
<h2>Loop-Austausch</h2>
<p><a href=/loop download=bananaphon.loop>Aktuellen Loop herunterladen</a></p>
<form id=lf method=POST action=/loop enctype=multipart/form-data>
<input type=file name=loop accept=.loop required><br>
<button type=submit>Loop hochladen &amp; abspielen</button></form>
<div id=ls></div>
<p class=hint>Der geladene Loop startet sofort und ersetzt den aktuellen.</p>
<script>
function $(i){return document.getElementById(i);}
$('f').onsubmit=function(e){e.preventDefault();var x=new XMLHttpRequest();x.open('POST','/update');
$('p').hidden=false;x.upload.onprogress=function(ev){if(ev.lengthComputable){
$('p').value=ev.loaded/ev.total*100;$('s').textContent=Math.round($('p').value)+' %';}};
x.onload=function(){$('s').textContent=x.status==200?'Fertig - Neustart ...':'Fehler: '+x.responseText;};
x.onerror=function(){$('s').textContent='Verbindung verloren (evtl. Neustart)';};
x.send(new FormData($('f')));};
$('lf').onsubmit=function(e){e.preventDefault();var x=new XMLHttpRequest();x.open('POST','/loop');
x.onload=function(){$('ls').textContent=x.status==200?'Loop geladen':'Fehler: '+x.responseText;};
x.onerror=function(){$('ls').textContent='Verbindung verloren';};
x.send(new FormData($('lf')));};
</script></body></html>)HTML";

// HTTP-Basic-Auth, falls ein OTA_PASSWORD gesetzt ist. Gibt false zurück
// und fordert die Anmeldung an, wenn die Berechtigung fehlt.
static bool authOk()
{
    if (OTA_PASSWORD[0] == '\0')
    {
        return true;
    }

    if (otaServer.authenticate("admin", OTA_PASSWORD))
    {
        return true;
    }

    otaServer.requestAuthentication();

    return false;
}

static void sendPage()
{
    if (!authOk())
    {
        return;
    }

    otaServer.send_P(200, "text/html", OTA_PAGE);
}

void OtaController::begin()
{
    self = this;
}

void OtaController::startServices()
{
    // ArduinoOTA (Push aus PlatformIO). begin() bringt den mDNS-
    // Responder hoch (bananaphon.local) und meldet _arduino._tcp an.
    // MDNS.begin() ist idempotent — ein zweiter Aufruf (RTP-MIDI nutzt
    // denselben Namen) ist ein harmloser No-op.
    ArduinoOTA.setHostname(MIDI_DEVICE_NAME);

    if (OTA_PASSWORD[0] != '\0')
    {
        ArduinoOTA.setPassword(OTA_PASSWORD);
    }

    ArduinoOTA.onStart([] { self->beginUpdateUi(); });
    ArduinoOTA.onProgress([](unsigned int done, unsigned int total)
                          { self->reportProgress(done, total); });
    ArduinoOTA.onEnd([] { self->endUpdateUi(); });
    ArduinoOTA.onError([](ota_error_t) { self->errorUpdateUi(); });

    ArduinoOTA.begin();

    // Web-Upload (Core-WebServer + Update.h — keine zusätzliche Lib)
    otaServer.collectHeaders(OTA_HEADERS, 1);

    otaServer.on("/", HTTP_GET, sendPage);
    otaServer.on("/update", HTTP_GET, sendPage);
    otaServer.on(
        "/update", HTTP_POST, [] { self->handleWebPost(); }, [] { self->handleWebUpload(); });

    // Loop-Austausch: GET lädt den aktuellen Loop herunter, POST lädt
    // einen hoch (nur wenn ein Save/Load-Callback verdrahtet ist)
    otaServer.on("/loop", HTTP_GET, [] { self->handleLoopDownload(); });
    otaServer.on(
        "/loop", HTTP_POST, [] { self->handleLoopPost(); }, [] { self->handleLoopUpload(); });

    otaServer.begin();

    MDNS.addService("http", "tcp", OTA_WEB_PORT);

    _started = true;

    Serial.print(
        "OTA bereit — Push: pio run -e lilygo-t-display-s3-ota -t upload  |  Web: http://");
    Serial.print(WiFi.localIP());
    Serial.println("/");
}

void OtaController::update()
{
    if (!ENABLE_OTA)
    {
        return;
    }

    if (!_started)
    {
        // Wie tryStartRTP(): erst starten, wenn das WLAN steht
        if (WiFi.status() == WL_CONNECTED)
        {
            startServices();
        }

        return;
    }

    ArduinoOTA.handle();

    otaServer.handleClient();
}

void OtaController::beginUpdateUi()
{
    _lastPercent = -1;

    if (_onStart)
    {
        _onStart();
    }
}

void OtaController::endUpdateUi()
{
    if (_onEnd)
    {
        _onEnd();
    }
}

void OtaController::errorUpdateUi()
{
    if (_onError)
    {
        _onError();
    }
}

void OtaController::reportProgress(uint32_t done, uint32_t total)
{
    if (!_onProgress)
    {
        return;
    }

    uint8_t percent =
        total > 0 ? static_cast<uint8_t>(static_cast<uint64_t>(done) * 100 / total) : 0;

    if (percent > 100)
    {
        percent = 100;
    }

    // Nur bei Änderung neu zeichnen — sonst flackert das Display bei
    // jedem Chunk und bremst den Upload aus
    if (static_cast<int16_t>(percent) == _lastPercent)
    {
        return;
    }

    _lastPercent = percent;

    _onProgress(percent);
}

void OtaController::handleWebUpload()
{
    HTTPUpload& up = otaServer.upload();

    if (up.status == UPLOAD_FILE_START)
    {
        // Ohne gültige Anmeldung nichts in den Flash schreiben —
        // handleWebPost() fordert danach die Auth an
        if (OTA_PASSWORD[0] != '\0' && !otaServer.authenticate("admin", OTA_PASSWORD))
        {
            return;
        }

        _contentLength = otaServer.header("Content-Length").toInt();

        beginUpdateUi();

        // UPDATE_SIZE_UNKNOWN: Update.h ermittelt und prüft die Größe
        // selbst und schreibt in die inaktive OTA-Partition
        if (!Update.begin(UPDATE_SIZE_UNKNOWN))
        {
            errorUpdateUi();
        }
    }
    else if (up.status == UPLOAD_FILE_WRITE)
    {
        if (Update.isRunning() && Update.write(up.buf, up.currentSize) != up.currentSize)
        {
            errorUpdateUi();
        }

        reportProgress(up.totalSize, _contentLength);
    }
    else if (up.status == UPLOAD_FILE_END)
    {
        if (Update.end(true))
        {
            reportProgress(1, 1); // 100 %
        }
        else
        {
            errorUpdateUi();
        }
    }
    else if (up.status == UPLOAD_FILE_ABORTED)
    {
        Update.abort();

        errorUpdateUi();
    }
}

void OtaController::handleWebPost()
{
    if (OTA_PASSWORD[0] != '\0' && !otaServer.authenticate("admin", OTA_PASSWORD))
    {
        otaServer.requestAuthentication();

        return;
    }

    bool ok = !Update.hasError();

    otaServer.sendHeader("Connection", "close");

    otaServer.send(ok ? 200 : 500, "text/plain", ok ? "OK" : "Update fehlgeschlagen");

    if (ok)
    {
        // dem Browser einen Moment für die Antwort lassen, dann neu
        // starten — das Gerät bootet in die frisch geflashte Partition
        delay(600);

        ESP.restart();
    }
}

// ------------------------------------------------
// Loop-Austausch
// ------------------------------------------------

void OtaController::handleLoopDownload()
{
    if (!authOk())
    {
        return;
    }

    size_t n = _onLoopSave ? _onLoopSave(loopBuf, sizeof(loopBuf)) : 0;

    if (n == 0)
    {
        otaServer.send(204, "text/plain", "Kein Loop aufgenommen");

        return;
    }

    otaServer.sendHeader("Content-Disposition", "attachment; filename=bananaphon.loop");
    otaServer.setContentLength(n);
    otaServer.send(200, "application/octet-stream", "");
    otaServer.sendContent(reinterpret_cast<const char*>(loopBuf), n);
}

void OtaController::handleLoopUpload()
{
    const HTTPUpload& up = otaServer.upload();

    if (up.status == UPLOAD_FILE_START)
    {
        if (OTA_PASSWORD[0] != '\0' && !otaServer.authenticate("admin", OTA_PASSWORD))
        {
            return; // handleLoopPost fordert dann die Auth an
        }

        _loopUpLen = 0;
    }
    else if (up.status == UPLOAD_FILE_WRITE)
    {
        // In den Puffer sammeln; zu große Uploads (mehr als ein Loop
        // fassen kann) werden abgeschnitten und beim Laden abgelehnt
        if (_loopUpLen + up.currentSize <= sizeof(loopBuf))
        {
            memcpy(loopBuf + _loopUpLen, up.buf, up.currentSize);

            _loopUpLen += up.currentSize;
        }
    }
}

void OtaController::handleLoopPost()
{
    if (OTA_PASSWORD[0] != '\0' && !otaServer.authenticate("admin", OTA_PASSWORD))
    {
        otaServer.requestAuthentication();

        return;
    }

    bool ok = _onLoopLoad && _onLoopLoad(loopBuf, _loopUpLen);

    otaServer.send(ok ? 200 : 400, "text/plain", ok ? "OK" : "Ungueltige Loop-Datei");
}
