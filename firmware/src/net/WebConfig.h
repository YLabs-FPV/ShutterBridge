// Wi-Fi SoftAP + config web UI (serves data/ from LittleFS + a JSON API)
// Wi-Fi and BLE share the radio, so the AP can shut down while armed
#pragma once

#include <WebServer.h>

#include "camera/Camera.h"
#include "config/Settings.h"
#include "fc/FlightController.h"

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

   private:
    void startAp();
    void stopAp();
    void handleConfig();  // GET /config.json
    void handleSave();    // POST /save
    void handleStatus();  // GET /status.json
    void handleScan();    // GET /scan  (BLE scan for cameras of the selected type)
    void handlePreset();  // GET /preset (GoPro-only preset switching)
    void handleMode();    // GET /mode?m=photo|video (photo/video switch)

    Settings&           s_;
    WebServer           server_;
    Camera*             camera_    = nullptr;
    const CameraStatus* camStatus_ = nullptr;
    const RcState*      rc_        = nullptr;
    bool                apUp_      = false;
    bool                routesSet_ = false;
    bool                dirty_     = false;
    bool                lastArmed_ = false;
    bool                wantAp_    = true;
    uint32_t            wantSince_ = 0;

    // Async BLE scan state - /scan never blocks the loop (see handleScan).
    enum ScanState { SCAN_IDLE, SCAN_RUNNING };
    ScanState scanState_   = SCAN_IDLE;
    uint32_t  scanStartMs_ = 0;
    int       scanType_    = 0;  // camera type filter captured when the scan started
};
