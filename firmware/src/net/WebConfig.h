// Wi-Fi SoftAP + config web UI (serves data/ from LittleFS + a JSON API)
// Wi-Fi and BLE share the radio, so the AP can shut down while armed
#pragma once

#include <DNSServer.h>
#include <ESPAsyncWebServer.h>

#include <deque>
#include <mutex>

#include "camera/Camera.h"
#include "config/Settings.h"
#include "fc/FlightController.h"
#include "net/ApiTypes.h"

class WebConfig {
   public:
    explicit WebConfig(Settings& s) : s_(s), server_(80) {
    }

    void begin();
    void poll(bool armed);  // manage AP up/down + service HTTP
    bool apActive() const {
        return apUp_;
    }
    bool clientConnected() const;

    // Live data sources for /status.json
    void attachStatus(const CameraStatus* cam, const RcState* rc) {
        camStatus_ = cam;
        rc_        = rc;
    }

    void attachCamera(Camera* cam) {
        camera_ = cam;
    }

    // True once after the user saves; main consumes it to re-apply runtime config
    bool takeDirty() {
        bool d = dirty_;
        dirty_ = false;
        return d;
    }

    // True once after the user hits the Betaflight-bridge button; main enters bridge mode
    bool takeBridgeRequest() {
        bool b     = bridgeReq_;
        bridgeReq_ = false;
        return b;
    }

    // Main flags this on entry so /status.json reports it and a page reload (F5)
    // can restore the bridge overlay instead of looking like normal operation
    void setBridgeActive(bool v) {
        bridgeActive_ = v;
    }

    // Runs an API route outside HTTP (the serial console). Call from the loop task only
    ApiReply call(const char* path, const ApiArgs& args);

   private:
    void startAp();
    void stopAp();
    using Handler = ApiReply (WebConfig::*)(const ApiArgs&);
    void defer(AsyncWebServerRequest* r, Handler h);
    void runDeferred();

    struct Route {
        const char* path;
        Handler     handler;
        bool        postOnly;  // HTTP method restriction; the serial console ignores it
    };
    static const Route kRoutes[];

    ApiReply handleConfig(const ApiArgs& a);   // GET /config.json
    ApiReply handleSave(const ApiArgs& a);     // POST /save
    ApiReply handleStatus(const ApiArgs& a);   // GET /status.json
    ApiReply handleScan(const ApiArgs& a);     // GET /scan  (BLE scan for cameras)
    ApiReply handlePreset(const ApiArgs& a);   // GET /preset (GoPro-only preset switching)
    ApiReply handleMode(const ApiArgs& a);     // GET /mode?m=photo|video (photo/video switch)
    ApiReply handleShutter(const ApiArgs& a);  // GET /shutter?a=start|stop|photo (all backends)
    ApiReply handleBridge(const ApiArgs& a);   // GET /bridge (enter Betaflight-bridge mode)
    ApiReply handleReboot(const ApiArgs& a);   // GET /reboot

    Settings&           s_;
    AsyncWebServer      server_;
    DNSServer           dns_;  // answers every name with the AP address (captive portal)
    Camera*             camera_       = nullptr;
    const CameraStatus* camStatus_    = nullptr;
    const RcState*      rc_           = nullptr;
    bool                apUp_         = false;
    bool                routesSet_    = false;
    bool                dirty_        = false;
    bool                bridgeReq_    = false;
    bool                bridgeActive_ = false;
    bool                lastArmed_    = false;
    bool                wantAp_       = true;
    uint32_t            wantSince_    = 0;
    uint32_t            rebootAtMs_   = 0;

    // API requests arrive on the AsyncTCP task; they are paused there and answered from poll()
    // so handlers keep running on the loop core alongside the rest of the app
    struct Pending {
        AsyncWebServerRequestPtr req;
        Handler                  handler;
    };
    std::deque<Pending> pending_;
    std::mutex          pendingMu_;

    // Async BLE scan state - /scan never blocks the loop (see handleScan).
    enum ScanState { SCAN_IDLE, SCAN_RUNNING };
    ScanState scanState_   = SCAN_IDLE;
    uint32_t  scanStartMs_ = 0;
    int       scanType_    = 0;  // camera type filter captured when the scan started
};
