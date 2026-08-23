#include "net/WebConfig.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESPmDNS.h>
#include <LittleFS.h>
#include <NimBLEDevice.h>
#include <WiFi.h>
#include <esp_coexist.h>

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
}  // namespace

void WebConfig::begin() {
    if (routesSet_)
        return;

    if (!LittleFS.begin(/*formatOnFail=*/true)) {
        Serial.println("[web] LittleFS mount failed");
    } else if (!LittleFS.exists("/index.html")) {
        Serial.println("[web] WARNING: web assets missing - run `pio run -t uploadfs`");
    }

    server_.serveStatic("/", LittleFS, "/index.html");
    server_.serveStatic("/style.css", LittleFS, "/style.css");
    server_.serveStatic("/app.js", LittleFS, "/app.js");

    server_.on("/config.json", [this]() { handleConfig(); });
    server_.on("/status.json", [this]() { handleStatus(); });
    server_.on("/scan", [this]() { handleScan(); });
    server_.on("/preset", [this]() { handlePreset(); });
    server_.on("/mode", [this]() { handleMode(); });
    server_.on("/save", HTTP_POST, [this]() { handleSave(); });
    server_.on("/reboot", [this]() {
        server_.send(200, "text/plain", "rebooting");
        delay(200);
        ESP.restart();
    });
    routesSet_ = true;
}

void WebConfig::startAp() {
    WiFi.mode(WIFI_AP);
    WiFi.softAPConfig(IPAddress(10, 0, 0, 1), IPAddress(10, 0, 0, 1), IPAddress(255, 255, 255, 0));
    const char* pass = (strlen(s_.apPass) >= 8) ? s_.apPass : nullptr;  // else open
    WiFi.softAP(s_.apSsid, pass);
    // IMPORTANT: do NOT disable Wi-Fi modem sleep (WiFi.setSleep(false)/WIFI_PS_NONE) here.
    // Wi-Fi/BLE coexistence requires modem sleep to stay on - it's the mechanism that hands
    // the shared antenna between the two stacks. Forcing it off starves Wi-Fi (page times out)
    // and can crash NimBLE. The Web UI lag is solved instead by pausing BLE connect attempts
    // while the AP is up
    begin();
    server_.begin();
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
    server_.stop();
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
        server_.handleClient();
    // Reconnect throttling in the camera backends keys off this: only slow BLE reconnects
    // when someone is actually on the Web UI
    g_apHasClient = apUp_ && WiFi.softAPgetStationNum() > 0;
}

void WebConfig::handleConfig() {
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
    doc["fw"]    = FW_VERSION;

    String out;
    serializeJson(doc, out);
    server_.send(200, "application/json", out);
}

void WebConfig::handleStatus() {
    JsonDocument doc;
    if (camStatus_) {
        const CameraStatus& s = *camStatus_;
        bool online           = s.connected && s.lastUpdateMs && (millis() - s.lastUpdateMs < 2500);
        doc["online"]         = online ? 1 : 0;
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
    server_.send(200, "application/json", out);
}

void WebConfig::handleSave() {
    for (int i = 0; i < 4; i++) {
        int v = server_.arg("s" + String(i)).toInt();
        if (v >= 0 && v < (int)OsdField::_Count)
            s_.osdSlot[i] = (OsdField)v;
    }

    for (int i = 0; i < FUNC_COUNT; i++) {
        const String p  = "m" + String(i);
        s_.modes[i].aux = (uint8_t)constrain(server_.arg(p + "aux").toInt(), 0, 14);
        uint16_t lo     = (uint16_t)constrain(server_.arg(p + "min").toInt(), 900, 2100);
        uint16_t hi     = (uint16_t)constrain(server_.arg(p + "max").toInt(), 900, 2100);
        if (hi < lo) {
            uint16_t t = lo;
            lo         = hi;
            hi         = t;
        }
        s_.modes[i].rangeMin = lo;
        s_.modes[i].rangeMax = hi;
    }

    s_.opMode             = (uint8_t)constrain(server_.arg("op").toInt(), 0, 1);
    s_.shutterVideoMode   = (uint8_t)constrain(server_.arg("svm").toInt(), 0, 1);
    s_.modeSwitchStyle    = (uint8_t)constrain(server_.arg("msm").toInt(), 0, 1);
    s_.recordStartDelayMs = (uint16_t)constrain(server_.arg("rdly").toInt(), 0, 60000);
    s_.recordStopDelayMs  = (uint16_t)constrain(server_.arg("sdly").toInt(), 0, 60000);
    s_.tzOffsetMin        = (int16_t)constrain(server_.arg("tz").toInt(), -720, 840);
    s_.clockSync          = server_.hasArg("clk") ? 1 : 0;
    s_.camType            = (uint8_t)constrain(server_.arg("ctype").toInt(), 0, 3);
    setStr(s_.camMac, sizeof(s_.camMac), server_.arg("mac"));
    setStr(s_.apSsid, sizeof(s_.apSsid), server_.arg("ssid"));
    setStr(s_.apPass, sizeof(s_.apPass), server_.arg("pass"));
    s_.webuiDisarmedOnly = server_.hasArg("dis") ? 1 : 0;
    s_.bleTxPower        = (uint8_t)constrain(server_.arg("ble").toInt(), 0, 2);

    s_.save();
    dirty_ = true;
    server_.send(200, "text/plain", "ok");
}

void WebConfig::handleScan() {
    // Async so the ~4 s scan doesn't block the loop: kick it off, answer {"scanning":1} until
    // done, then return the result array. The front-end polls until it gets the array
    constexpr uint32_t SCAN_MS = 4000;
    NimBLEScan*        scan    = NimBLEDevice::getScan();
    const uint32_t     now     = millis();

    if (scanState_ == SCAN_IDLE) {
        scanType_ = server_.hasArg("type") ? server_.arg("type").toInt() : s_.camType;
        scan->setActiveScan(true);
        scan->clearResults();
        scan->start(SCAN_MS, /*isContinue=*/false, /*restart=*/true);  // returns immediately
        scanState_   = SCAN_RUNNING;
        scanStartMs_ = now;
        server_.send(202, "application/json", "{\"scanning\":1}");
        return;
    }

    if (scan->isScanning() && (now - scanStartMs_) < SCAN_MS + 1000) {
        server_.send(202, "application/json", "{\"scanning\":1}");
        return;
    }

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
    server_.send(200, "application/json", out);
}

void WebConfig::handlePreset() {
    // GoPro-only: /preset?group=0|1|2 (video/photo/timelapse) or /preset?id=<hex|dec>
    if (!camera_ || !camera_->supportsPresets()) {
        server_.send(409, "text/plain", "no preset-capable camera");
        return;
    }
    if (!camera_->isConnected()) {
        server_.send(409, "text/plain", "camera offline");
        return;
    }
    bool ok = false;
    if (server_.hasArg("group")) {
        const int g = server_.arg("group").toInt();
        if (g >= 0 && g <= 2)
            ok = camera_->loadPresetGroup((PresetGroup)g);
    } else if (server_.hasArg("id")) {
        // Accept 0x-prefixed hex or plain decimal
        const String   v  = server_.arg("id");
        const uint32_t id = (uint32_t)strtoul(v.c_str(), nullptr, 0);
        ok                = camera_->loadPreset(id);
    }
    server_.send(ok ? 200 : 400, "text/plain", ok ? "ok" : "bad preset request");
}

void WebConfig::handleMode() {
    // /mode?m=photo|video - photo/video switch. Action 4/5 (R SDK) + GoPro; Osmo Nano can't
    if (!camera_ || !camera_->isConnected()) {
        server_.send(409, "text/plain", "camera offline");
        return;
    }
    if (!server_.hasArg("m")) {
        server_.send(400, "text/plain", "need ?m=photo|video");
        return;
    }
    const String  m  = server_.arg("m");
    const CamMode cm = (m == "photo" || m == "0") ? CamMode::Photo : CamMode::Video;
    const bool    ok = camera_->setMode(cm);
    server_.send(ok ? 200 : 400, "text/plain", ok ? "ok" : "setMode failed / unsupported");
}
