// DJI Action-series backend (Osmo Action 4/5/6).
#pragma once

#include <NimBLEDevice.h>

#include <string>

#include "camera/Camera.h"
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

   private:
    enum ProtoState { PROTO_NONE, PROTO_CONNECTING, PROTO_CONNECTED };

    bool attempt();
    bool connect();
    void sendConnectionRequest();
    void subscribeStatus();
    bool recordCtrl(uint8_t ctrl);  // 0 = start, 1 = stop
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
    ProtoState                proto_         = PROTO_NONE;
    volatile bool             connected_     = false;
    uint16_t                  seq_           = 1;
    uint16_t                  verifyData_    = 0;
    uint32_t                  lastAttemptMs_ = 0;
    uint32_t                  connectedAtMs_ = 0;
    static constexpr uint32_t kBackoffMinMs  = 5000;
    static constexpr uint32_t kBackoffMaxMs  = 30000;
    uint32_t                  backoffMs_     = kBackoffMinMs;
};
