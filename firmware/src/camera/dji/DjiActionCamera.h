// DJI Action-series backend (Osmo Action 4/5/6).
#pragma once

#include <NimBLEDevice.h>

#include <string>

#include "camera/Camera.h"
#include "camera/BootWake.h"
#include "djir.h"
#include "duml.h"

class DjiActionCamera : public Camera {
   public:
    explicit DjiActionCamera(const char* macAddress);

    bool                begin() override;
    void                poll() override;
    bool                startRecord() override;
    bool                stopRecord() override;
    bool                takePhoto() override;
    bool                setMode(CamMode mode) override;
    const CameraStatus& status() const override {
        return status_;
    }
    bool isConnected() const override {
        return connected_;
    }

    // Power: there's no advertisement to read the camera's state from, so "asleep" means it was
    // seen entering sleep mode (or sent there) less than 30 min ago - the wake broadcast's window
    bool isAsleep() const override {
        return !awake() && asleepKnown();
    }
    bool isVisible() const override {
        return connected_ || asleepKnown() || wake_.active();
    }
    bool isWaking() const override {
        const uint32_t at = deferredRecAtMs_;
        return wake_.active() || (at != 0 && millis() - at < 5000);
    }
    bool powerAware() const override {
        return true;
    }
    bool canSleep() const override {
        return true;
    }
    bool wake() override;
    bool sleep() override;

   private:
    enum ProtoState { PROTO_NONE, PROTO_CONNECTING, PROTO_CONNECTED };

    bool attempt();
    bool connect();
    void sendConnectionRequest();
    void subscribeStatus();
    bool recordCtrl(uint8_t ctrl);  // 0 = start, 1 = stop
    bool awake() const;             // connected, reporting, and not in sleep mode
    void updateBootWake(uint32_t now);
    void updatePendingSleep(uint32_t now);
    bool asleepKnown() const;
    void advertiseWake();
    bool writeRaw(const uint8_t* data, size_t len);
    bool sendR(uint8_t cmdSet, uint8_t cmdId, uint8_t cmdType, const uint8_t* pl, size_t n,
               uint16_t seq);
    void onNotify(const uint8_t* data, size_t len);
    void handleR(const djir::Frame& f);     // R SDK (0xAA)
    void handleDuml(const duml::Frame& f);  // DUML (0x55) - battery only

    class ClientCB;

    std::string                 mac_;
    NimBLEAddress               addr_;
    bool                        haveAddr_ = false;
    NimBLEClient*               client_   = nullptr;
    NimBLERemoteCharacteristic* chWrite_  = nullptr;  // fff5
    NimBLERemoteCharacteristic* chNotify_ = nullptr;  // fff4
    ClientCB*                   cb_       = nullptr;

    djir::FrameScanner        rscan_;  // 0xAA
    duml::FrameScanner        dscan_;  // 0x55 (battery)
    CameraStatus              status_;
    ProtoState                proto_      = PROTO_NONE;
    volatile bool             connected_  = false;
    uint16_t                  seq_        = 1;
    uint16_t                  verifyData_ = 0;
    uint8_t                   powerMode_  = 0;  // from the status push: 0 normal, 3 sleep
    BootWake                  wake_;
    uint32_t                  lastWakeAdvMs_ = 0;
    uint32_t                  asleepAtMs_    = 0;      // sleep seen/sent; 0 = not known asleep
    bool                      wakeRecord_    = false;  // start recording once the wake completes
    volatile uint32_t         deferredRecAtMs_ = 0;    // deferred record sent; 0 = none
    uint32_t                  sleepReqMs_      = 0;    // sleep requested, waiting for rec to stop
    uint32_t                  lastAttemptMs_ = 0;
    uint32_t                  connectedAtMs_ = 0;
    static constexpr uint32_t kBackoffMinMs  = 5000;
    static constexpr uint32_t kBackoffMaxMs  = 30000;
    uint32_t                  backoffMs_     = kBackoffMinMs;
};
