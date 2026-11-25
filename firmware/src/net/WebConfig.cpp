#include "net/WebConfig.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESPmDNS.h>
#include <LittleFS.h>
#include <NimBLEDevice.h>
#include <WiFi.h>
#include <esp_coexist.h>

#include "Board.h"
#include "config.h"
#include "net/RadioCoex.h"

// Shared Wi-Fi/BLE radio hint. True while the AP is serving
volatile bool g_apActive    = false;
volatile bool g_apHasClient = false;

namespace {
    void setStr(char* dst, size_t cap, const String& v) {
        strncpy(dst, v.c_str(), cap - 1);
        dst[cap - 1] = '\0';
    }

    const IPAddress kApIp(10, 0, 0, 1);

    // True for requests addressed to this device; anything else arrived via the wildcard DNS
    bool isOwnHost(AsyncWebServerRequest* r) {
        String    host  = r->host();
        const int colon = host.indexOf(':');
        if (colon >= 0)
            host.remove(colon);
        return host.isEmpty() || host == kApIp.toString() ||
               host.equalsIgnoreCase("shutterbridge.local");
    }

    class HttpArgs : public ApiArgs {
       public:
        explicit HttpArgs(AsyncWebServerRequest* r) : r_(r) {
        }
        bool has(const String& key) const override {
            return r_->hasArg(key.c_str());
        }
        String get(const String& key) const override {
            return r_->arg(key);
        }

       private:
        AsyncWebServerRequest* r_;
    };
}  // namespace

const WebConfig::Route WebConfig::kRoutes[] = {
    {"/config.json", &WebConfig::handleConfig, false},
    {"/status.json", &WebConfig::handleStatus, false},
    {"/scan", &WebConfig::handleScan, false},
    {"/preset", &WebConfig::handlePreset, false},
    {"/mode", &WebConfig::handleMode, false},
    {"/shutter", &WebConfig::handleShutter, false},
    {"/power", &WebConfig::handlePower, false},
    {"/bridge", &WebConfig::handleBridge, false},
    {"/save", &WebConfig::handleSave, true},
    {"/reboot", &WebConfig::handleReboot, false},
};

ApiReply WebConfig::call(const char* path, const ApiArgs& args) {
    for (const Route& rt : kRoutes)
        if (strcmp(rt.path, path) == 0)
            return (this->*rt.handler)(args);
    return ApiReply::text(404, "not found");
}

void WebConfig::begin() {
    if (routesSet_)
        return;

    if (!LittleFS.begin(/*formatOnFail=*/true)) {
        Serial.println("[web] LittleFS mount failed");
    } else if (!LittleFS.exists("/index.html.gz") && !LittleFS.exists("/index.html")) {
        Serial.println("[web] WARNING: web assets missing - run `pio run -t uploadfs`");
    }

    for (const Route& rt : kRoutes) {
        const Handler h = rt.handler;
        server_.on(rt.path, rt.postOnly ? HTTP_POST : HTTP_ANY,
                   [this, h](AsyncWebServerRequest* r) { defer(r, h); });
    }

    server_.serveStatic("/", LittleFS, "/")
        .setDefaultFile("index.html")
        .setCacheControl("no-cache")
        .setFilter(isOwnHost);
    // Captive portal: every name resolves here, so OS connectivity probes (and any other
    // foreign-host request) get redirected to the UI
    server_.onNotFound([](AsyncWebServerRequest* r) {
        if (!isOwnHost(r)) {
            r->redirect(String("http://") + kApIp.toString() + "/");
            return;
        }
        r->send(404, "text/plain", "not found");
    });
    routesSet_ = true;
}

void WebConfig::startAp() {
    WiFi.mode(WIFI_AP);
#ifdef WIFI_AP_TX_POWER
    WiFi.setTxPower(WIFI_AP_TX_POWER);
#endif
    WiFi.softAPConfig(kApIp, kApIp, IPAddress(255, 255, 255, 0));
    const char* pass = (strlen(s_.apPass) >= 8) ? s_.apPass : nullptr;  // else open
    if (!WiFi.softAP(s_.apSsid, pass))
        Serial.println("[web] softAP start failed");
    // IMPORTANT: do NOT disable Wi-Fi modem sleep (WiFi.setSleep(false)/WIFI_PS_NONE) here.
    // Wi-Fi/BLE coexistence requires modem sleep to stay on - it's the mechanism that hands
    // the shared antenna between the two stacks. Forcing it off starves Wi-Fi (page times out)
    // and can crash NimBLE. The Web UI lag is solved instead by pausing BLE connect attempts
    // while the AP is up
    begin();
    server_.begin();
    dns_.start(53, "*", WiFi.softAPIP());
    if (MDNS.begin("shutterbridge")) {  // -> http://shutterbridge.local
        MDNS.addService("http", "tcp", 80);
    }
    apUp_      = true;
    g_apActive = true;
    // Give Wi-Fi radio priority while the user is on the config page - a competing BLE
    // connect/scan otherwise starves the AP and the UI takes ~15 s per request
    esp_coex_preference_set(ESP_COEX_PREFER_WIFI);
    Serial.printf("[web] AP up: SSID \"%s\"  http://%s/  http://shutterbridge.local/\n", s_.apSsid,
                  WiFi.softAPIP().toString().c_str());
}

void WebConfig::stopAp() {
    MDNS.end();
    dns_.stop();
    server_.end();
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_OFF);
    apUp_      = false;
    g_apActive = false;
    esp_coex_preference_set(ESP_COEX_PREFER_BALANCE);  // BLE can have the radio again
    Serial.println("[web] AP down");
}

bool WebConfig::clientConnected() const {
    return apUp_ && WiFi.softAPgetStationNum() > 0;
}

void WebConfig::poll(bool armed) {
    lastArmed_ = armed;
    // Debounce the AP toggle: Wi-Fi churn disturbs MSP timing, jittering `armed`
    // into a flap loop
    const bool     want = !(s_.webuiDisarmedOnly && armed);
    const uint32_t now  = millis();
    if (want != wantAp_) {
        wantAp_    = want;
        wantSince_ = now;
    } else if (want != apUp_ && (now - wantSince_) > 2000) {
        if (want)
            startAp();
        else
            stopAp();
    }
    if (apUp_)
        runDeferred();
    // Reconnect throttling in the camera backends keys off this: only slow BLE reconnects
    // when someone is actually on the Web UI
    g_apHasClient = apUp_ && WiFi.softAPgetStationNum() > 0;

    // Deferred self-reboot after a config save (signed compare tolerates wrap)
    if (rebootAtMs_ && (int32_t)(now - rebootAtMs_) >= 0)
        ESP.restart();
}

void WebConfig::defer(AsyncWebServerRequest* r, Handler h) {
    std::lock_guard<std::mutex> lock(pendingMu_);
    pending_.push_back({r->pause(), h});
}

// A request whose client went away meanwhile has expired and is dropped
void WebConfig::runDeferred() {
    std::deque<Pending> batch;
    {
        std::lock_guard<std::mutex> lock(pendingMu_);
        batch.swap(pending_);
    }
    for (Pending& p : batch) {
        if (auto r = p.req.lock()) {
            const ApiReply out = (this->*p.handler)(HttpArgs(r.get()));
            r->send(out.code, out.type, out.body);
        }
    }
}

ApiReply WebConfig::handleConfig(const ApiArgs& a) {
    JsonDocument doc;

    JsonArray osd = doc["osd"].to<JsonArray>();
    for (int i = 0; i < 4; i++)
        osd.add((int)s_.osdSlot[i]);

    JsonArray modes = doc["modes"].to<JsonArray>();
    for (int i = 0; i < FUNC_COUNT; i++) {
        const ModeRange& m = s_.modes[i];
        JsonObject       o = modes.add<JsonObject>();
        o["name"]          = funcName((Func)i);
        o["aux"]           = m.aux;
        o["min"]           = m.rangeMin;
        o["max"]           = m.rangeMax;
    }

    doc["op"]    = s_.opMode;  // 0 = manual, 1 = record on arm
    doc["ctype"] = s_.camType;
    doc["mac"]   = s_.camMac;
    doc["ssid"]  = s_.apSsid;
    doc["pass"]  = s_.apPass;
    doc["dis"]   = s_.webuiDisarmedOnly;
    doc["ble"]   = s_.bleTxPower;        // 0 low, 1 med, 2 high
    doc["svm"]   = s_.shutterVideoMode;  // 0=momentary, 1=2-pos
    doc["msm"]   = s_.modeSwitchStyle;   // 0=momentary, 1=2-pos
    doc["rdly"]  = s_.recordStartDelayMs;
    doc["sdly"]  = s_.recordStopDelayMs;
    doc["tz"]    = s_.tzOffsetMin;
    doc["clk"]   = s_.clockSync;  // sync camera clock from FC GPS time
    doc["wake"]  = s_.autoWake;   // wake a powered-off camera on boot
    doc["wrec"]  = s_.wakeOnRecord;  // wake a powered-off camera on a record request
    doc["fw"]    = FW_VERSION;
    doc["board"] = boardId();  // variant running now (resolved at boot)
    if (boardHasVariants())
        doc["bvar"] = s_.boardVariant;  // saved choice, 0 = auto

    // FC UART: saved pins (-1 = default), the board defaults and the GPIOs on offer
    doc["rx"]  = s_.fcRxPin;
    doc["tx"]  = s_.fcTxPin;
    doc["rxd"] = FC_RX_PIN;
    doc["txd"] = FC_TX_PIN;
    uint8_t      pins[48];
    const size_t n    = boardUartPins(pins, sizeof(pins));
    JsonArray    list = doc["pins"].to<JsonArray>();
    for (size_t i = 0; i < n; i++)
        list.add(pins[i]);

    String out;
    serializeJson(doc, out);
    return ApiReply::json(200, out);
}

ApiReply WebConfig::handleStatus(const ApiArgs& a) {
    if (bridgeActive_)
        return ApiReply::json(200, "{\"bridge\":1}");

    JsonDocument doc;
    if (camStatus_) {
        const CameraStatus& s = *camStatus_;
        bool online           = s.connected && s.lastUpdateMs && (millis() - s.lastUpdateMs < 2500);
        doc["online"]         = online ? 1 : 0;
        // Camera power state: online | waking | sleep (off, wakeable) | offline (not seen).
        // Backends that can't tell report online/offline only
        const char* cam = online ? "online" : "offline";
        if (!online && camera_ && camera_->powerAware()) {
            if (!camera_->isVisible())
                cam = "offline";
            else if (camera_->isWaking())
                cam = "waking";
            else if (camera_->isAsleep())
                cam = "sleep";
            else
                cam = "waking";  // on and advertising, link coming up
        }
        doc["cam"]   = cam;
        doc["power"]  = camera_ && camera_->powerAware() ? 1 : 0;
        doc["psleep"] = camera_ && camera_->canSleep() ? 1 : 0;
        doc["conn"]   = camera_ && camera_->isConnected() ? 1 : 0;  // link up (maybe pre-telemetry)
        doc["recording"]      = s.isRecording() ? 1 : 0;
        doc["mode"]           = toString(s.mode);
        doc["res"]            = s.resolution;  // "" when unreported
        doc["fps"]            = s.fps;         // 0 when unreported
        doc["elapsed"]        = s.recElapsedS;
        doc["left"]           = s.recLeftS;
        doc["sd"]             = s.sdFreeMB;
        doc["batt"]           = s.batteryPct;
        doc["paired"]         = s.paired ? 1 : 0;
    } else {
        doc["online"] = 0;
    }
    doc["aux1"]  = rc_ ? rc_->aux(1) : 0;
    doc["armed"] = lastArmed_ ? 1 : 0;

    JsonArray ch = doc["ch"].to<JsonArray>();
    if (rc_)
        for (int i = 0; i < rc_->count; i++)
            ch.add(rc_->ch[i]);

    String out;
    serializeJson(doc, out);
    return ApiReply::json(200, out);
}

ApiReply WebConfig::handleSave(const ApiArgs& a) {
    // Boot-only fields: snapshot to detect a change that warrants a reboot
    const uint8_t oldCamType = s_.camType;
    const uint8_t oldBoard   = s_.boardVariant;
    const int8_t  oldRx      = s_.fcRxPin;
    const int8_t  oldTx      = s_.fcTxPin;
    char          oldMac[sizeof(s_.camMac)];
    char          oldSsid[sizeof(s_.apSsid)];
    char          oldPass[sizeof(s_.apPass)];
    strncpy(oldMac, s_.camMac, sizeof(oldMac));
    strncpy(oldSsid, s_.apSsid, sizeof(oldSsid));
    strncpy(oldPass, s_.apPass, sizeof(oldPass));

    for (int i = 0; i < 4; i++) {
        int v = a.get("s" + String(i)).toInt();
        if (v >= 0 && v < (int)OsdField::_Count)
            s_.osdSlot[i] = (OsdField)v;
    }

    for (int i = 0; i < FUNC_COUNT; i++) {
        const String p  = "m" + String(i);
        s_.modes[i].aux = (uint8_t)constrain(a.get(p + "aux").toInt(), 0, 14);
        uint16_t lo     = (uint16_t)constrain(a.get(p + "min").toInt(), 900, 2100);
        uint16_t hi     = (uint16_t)constrain(a.get(p + "max").toInt(), 900, 2100);
        if (hi < lo) {
            uint16_t t = lo;
            lo         = hi;
            hi         = t;
        }
        s_.modes[i].rangeMin = lo;
        s_.modes[i].rangeMax = hi;
    }

    s_.opMode             = (uint8_t)constrain(a.get("op").toInt(), 0, 1);
    s_.shutterVideoMode   = (uint8_t)constrain(a.get("svm").toInt(), 0, 1);
    s_.modeSwitchStyle    = (uint8_t)constrain(a.get("msm").toInt(), 0, 1);
    s_.recordStartDelayMs = (uint16_t)constrain(a.get("rdly").toInt(), 0, 60000);
    s_.recordStopDelayMs  = (uint16_t)constrain(a.get("sdly").toInt(), 0, 60000);
    s_.tzOffsetMin        = (int16_t)constrain(a.get("tz").toInt(), -720, 840);
    s_.clockSync          = a.has("clk") ? 1 : 0;
    s_.autoWake           = a.has("wake") ? 1 : 0;
    s_.wakeOnRecord       = a.has("wrec") ? 1 : 0;
    s_.camType            = (uint8_t)constrain(a.get("ctype").toInt(), 0, 3);
    setStr(s_.camMac, sizeof(s_.camMac), a.get("mac"));
    setStr(s_.apSsid, sizeof(s_.apSsid), a.get("ssid"));
    setStr(s_.apPass, sizeof(s_.apPass), a.get("pass"));
    s_.webuiDisarmedOnly = a.has("dis") ? 1 : 0;
    s_.bleTxPower        = (uint8_t)constrain(a.get("ble").toInt(), 0, 2);
    if (boardHasVariants() && a.has("bvar"))
        s_.boardVariant =
            (uint8_t)constrain(a.get("bvar").toInt(), 0, (int)BoardVariant::_Count - 1);
    if (a.has("rx") && a.has("tx")) {
        s_.fcRxPin = (int8_t)constrain(a.get("rx").toInt(), -1, 48);
        s_.fcTxPin = (int8_t)constrain(a.get("tx").toInt(), -1, 48);
        // Keep only a pair the board would actually use
        int rx, tx;
        boardFcPins(s_.fcRxPin, s_.fcTxPin, rx, tx);
        if ((s_.fcRxPin >= 0 && s_.fcRxPin != rx) || (s_.fcTxPin >= 0 && s_.fcTxPin != tx))
            s_.fcRxPin = s_.fcTxPin = -1;
    }

    s_.save();
    dirty_ = true;

    // Reboot after the response flushes if a boot-only field changed (write already committed)
    const bool needReboot = s_.camType != oldCamType || s_.boardVariant != oldBoard ||
                            s_.fcRxPin != oldRx || s_.fcTxPin != oldTx ||
                            strncmp(s_.camMac, oldMac, sizeof(oldMac)) != 0 ||
                            strncmp(s_.apSsid, oldSsid, sizeof(oldSsid)) != 0 ||
                            strncmp(s_.apPass, oldPass, sizeof(oldPass)) != 0;
    if (needReboot) {
        rebootAtMs_ = millis() + 600;
        if (!rebootAtMs_)
            rebootAtMs_ = 1;
        return ApiReply::text(200, "reboot");
    }
    return ApiReply::text(200, "ok");
}

ApiReply WebConfig::handleScan(const ApiArgs& a) {
    // Async so the ~4 s scan doesn't block the loop: kick it off, answer {"scanning":1} until
    // done, then return the result array. The front-end polls until it gets the array
    if (!NimBLEDevice::isInitialized())
        return ApiReply::json(200, "[]");

    constexpr uint32_t SCAN_MS = 4000;
    NimBLEScan*        scan    = NimBLEDevice::getScan();
    const uint32_t     now     = millis();

    if (scanState_ == SCAN_IDLE) {
        scanType_ = a.has("type") ? a.get("type").toInt() : s_.camType;
        scan->setActiveScan(true);
        scan->clearResults();
        scan->start(SCAN_MS, /*isContinue=*/false, /*restart=*/true);  // returns immediately
        scanState_   = SCAN_RUNNING;
        scanStartMs_ = now;
        return ApiReply::json(202, "{\"scanning\":1}");
    }

    if (scan->isScanning() && (now - scanStartMs_) < SCAN_MS + 1000)
        return ApiReply::json(202, "{\"scanning\":1}");

    // Done - classify each advertiser and report its backend type + model
    const NimBLEUUID  GOPRO_SVC((uint16_t)0xFEA6);
    NimBLEScanResults res = scan->getResults();  // accumulated results, no block

    JsonDocument doc;
    JsonArray    arr = doc.to<JsonArray>();
    for (int i = 0; i < res.getCount(); i++) {
        const NimBLEAdvertisedDevice* d    = res.getDevice(i);
        const String                  name = d->getName().c_str();

        int         type  = -1;
        const char* model = nullptr;

        // type: 0 = // Osmo/DUML, 1 = Action-series/R-SDK (Action 3/4/5, Osmo 360), 2 = GoPro
        if (d->isAdvertisingService(GOPRO_SVC) || name.startsWith("GoPro")) {
            type  = 2;
            model = "GoPro";
        } else {
            const std::string md = d->getManufacturerData();
            if (md.size() >= 3 && (uint8_t)md[0] == 0xAA && (uint8_t)md[1] == 0x08) {
                switch ((uint8_t)md[2]) {  // DJI model code
                    case 0x19:
                        type  = 0;
                        model = "Osmo Nano";
                        break;
                    case 0x12:
                        type  = 1;
                        model = "Osmo Action 3";
                        break;
                    case 0x14:
                        type  = 1;
                        model = "Osmo Action 4";
                        break;
                    case 0x15:
                        type  = 1;
                        model = "Osmo Action 5 Pro";
                        break;
                    case 0x17:
                        type  = 1;
                        model = "Osmo 360";
                        break;
                    default:
                        type  = 1;
                        model = "DJI camera";
                        break;  // unknown
                }
            }
        }
        if (type < 0)
            continue;  // not a recognized camera

        JsonObject o = arr.add<JsonObject>();
        o["mac"]     = d->getAddress().toString().c_str();
        o["name"]    = name;
        o["type"]    = type;
        o["model"]   = model;
    }
    scan->clearResults();
    scanState_ = SCAN_IDLE;

    String out;
    serializeJson(doc, out);
    return ApiReply::json(200, out);
}

ApiReply WebConfig::handlePreset(const ApiArgs& a) {
    // GoPro-only: /preset?group=0|1|2 (video/photo/timelapse) or /preset?id=<hex|dec>
    if (!camera_ || !camera_->supportsPresets())
        return ApiReply::text(409, "no preset-capable camera");
    if (!camera_->isConnected())
        return ApiReply::text(409, "camera offline");
    bool ok = false;
    if (a.has("group")) {
        const int g = a.get("group").toInt();
        if (g >= 0 && g <= 2)
            ok = camera_->loadPresetGroup((PresetGroup)g);
    } else if (a.has("id")) {
        // Accept 0x-prefixed hex or plain decimal
        const String   v  = a.get("id");
        const uint32_t id = (uint32_t)strtoul(v.c_str(), nullptr, 0);
        ok                = camera_->loadPreset(id);
    }
    return ApiReply::text(ok ? 200 : 400, ok ? "ok" : "bad preset request");
}

ApiReply WebConfig::handleMode(const ApiArgs& a) {
    // /mode?m=photo|video - photo/video switch on backends that implement setMode
    if (!camera_ || !camera_->isConnected())
        return ApiReply::text(409, "camera offline");
    if (!a.has("m"))
        return ApiReply::text(400, "need ?m=photo|video");
    const String  m  = a.get("m");
    const CamMode cm = (m == "photo" || m == "0") ? CamMode::Photo : CamMode::Video;
    const bool    ok = camera_->setMode(cm);
    return ApiReply::text(ok ? 200 : 400, ok ? "ok" : "setMode failed / unsupported");
}

ApiReply WebConfig::handleShutter(const ApiArgs& a) {
    if (!camera_ || !camera_->isConnected())
        return ApiReply::text(409, "camera offline");
    const String act = a.get("a");
    bool         ok;
    if (act == "start")
        ok = camera_->startRecord();
    else if (act == "stop")
        ok = camera_->stopRecord();
    else if (act == "photo")
        ok = camera_->takePhoto();
    else
        return ApiReply::text(400, "need ?a=start|stop|photo");
    return ApiReply::text(ok ? 200 : 400, ok ? "ok" : "command failed");
}

ApiReply WebConfig::handlePower(const ApiArgs& a) {
    // /power?a=on|off - wake a sleeping camera / stop recording and put it to sleep
    if (!camera_ || !camera_->powerAware())
        return ApiReply::text(409, "power control unsupported");
    const String act = a.get("a");
    if (act == "on") {
        if (!camera_->isVisible())
            return ApiReply::text(409, "camera not found");
        return camera_->wake() ? ApiReply::text(200, "ok") : ApiReply::text(400, "wake failed");
    }
    if (act == "off") {
        if (!camera_->canSleep())
            return ApiReply::text(409, "this camera can't be put to sleep remotely");
        return camera_->sleep() ? ApiReply::text(200, "ok") : ApiReply::text(409, "camera offline");
    }
    return ApiReply::text(400, "need ?a=on|off");
}

ApiReply WebConfig::handleBridge(const ApiArgs& a) {
    bridgeReq_ = true;
    return ApiReply::text(200, "bridge");
}

ApiReply WebConfig::handleReboot(const ApiArgs& a) {
    rebootAtMs_ = millis() + 300;  // let the response flush first
    if (!rebootAtMs_)
        rebootAtMs_ = 1;
    return ApiReply::text(200, "rebooting");
}
