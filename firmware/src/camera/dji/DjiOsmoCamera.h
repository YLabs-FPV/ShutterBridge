#pragma once

#include <NimBLEDevice.h>

#include <string>

#include "camera/Camera.h"
#include "duml.h"

class DjiOsmoCamera : public Camera {
   public:
    explicit DjiOsmoCamera(const char* macAddress, const char* namePrefix = "OsmoNano");

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
    bool attempt();         // resolve address (once) then connect
    bool resolveAddress();  // pinned MAC, or scan by name
    bool connect();
    void doHandshake();
    bool writeRaw(const uint8_t* data, size_t len);
    bool writeCmd(uint8_t cmdSet, uint8_t cmdId, const uint8_t* payload, size_t payloadLen);
    void onNotify(const uint8_t* data, size_t len);
    void handleFrame(const duml::Frame& f);

    class ClientCB;

    std::string                 mac_;
    std::string                 namePrefix_;
    NimBLEAddress               addr_;
    bool                        haveAddr_ = false;
    NimBLEClient*               client_   = nullptr;
    NimBLERemoteCharacteristic* chWrite_  = nullptr;  // fff5 (write-no-response)
    NimBLERemoteCharacteristic* chNotify_ = nullptr;  // fff4 (notify)
    ClientCB*                   cb_       = nullptr;

    duml::FrameScanner        scanner_;
    CameraStatus              status_;
    volatile bool             connected_     = false;
    uint16_t                  seq_           = 0x9000;
    uint32_t                  lastAttemptMs_ = 0;
    uint32_t                  connectedAtMs_ = 0;
    uint8_t                   lastWorkMode_  = 0xFF;  // for logging work-mode changes
    static constexpr uint32_t kBackoffMinMs  = 3000;
    static constexpr uint32_t kBackoffMaxMs  = 30000;
    uint32_t                  backoffMs_     = kBackoffMinMs;
};
