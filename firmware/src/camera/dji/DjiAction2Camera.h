// DJI Osmo Action 2 backend: DUML over BLE with the app-level pairing the DJI Mimo app uses.
// The Action 2 predates the R-SDK that DjiActionCamera speaks, and unlike the Osmo Nano it
// asks for an on-screen approval on first connect. Pairing flow and record commands follow
// shutterlink (https://github.com/rover1312/shutterlink, MIT, (c) 2026 rover1312).
// No decoded record state/time comes back, so both track our own acknowledged start/stop
#pragma once

#include <NimBLEDevice.h>

#include <string>

#include "camera/Camera.h"
#include "duml.h"

class DjiAction2Camera : public Camera {
   public:
    explicit DjiAction2Camera(const char* macAddress);

    bool                begin() override;
    void                poll() override;
    bool                startRecord() override;
    bool                stopRecord() override;
    bool                takePhoto() override;
    const CameraStatus& status() const override {
        return status_;
    }
    bool isConnected() const override {
        return connected_;
    }

   private:
    bool attempt();
    bool connect();
    void sendPairing();
    void keepAlive();
    void setApproved(const char* how);
    void drop(const char* why);
    bool recordCtrl(bool start);
    bool writeRaw(const uint8_t* data, size_t len);
    bool writeCmd(uint8_t receiver, uint8_t cmdSet, uint8_t cmdId, const uint8_t* payload,
                  size_t payloadLen);
    void onNotify(const uint8_t* data, size_t len);
    void handleFrame(const duml::Frame& f);

    class ClientCB;

    std::string                 mac_;
    NimBLEAddress               addr_;
    bool                        haveAddr_ = false;
    NimBLEClient*               client_   = nullptr;
    NimBLERemoteCharacteristic* chWrite_  = nullptr;  // fff5 (write-no-response)
    NimBLERemoteCharacteristic* chNotify_ = nullptr;  // fff4 (pairing arm + notify)
    ClientCB*                   cb_       = nullptr;

    duml::FrameScanner        scanner_;
    CameraStatus              status_;
    volatile bool             connected_     = false;  // BLE link up
    volatile bool             approved_      = false;  // pairing accepted, commands work
    bool                      lastCmdStart_  = false;
    uint32_t                  recStartMs_    = 0;
    uint16_t                  seq_           = 1;
    uint32_t                  lastAttemptMs_ = 0;
    uint32_t                  connectedAtMs_ = 0;
    uint32_t                  lastKeepMs_    = 0;
    static constexpr uint32_t kBackoffMinMs  = 3000;
    static constexpr uint32_t kBackoffMaxMs  = 30000;
    uint32_t                  backoffMs_     = kBackoffMinMs;
};
