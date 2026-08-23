// GoPro backend over the Open GoPro BLE API. https://gopro.github.io/OpenGoPro/ble/
// Length-prefixed TLV over the Control & Query service (0xFEA6); large messages
// span multiple 20-byte packets with a continuation header (see Reassembler)
#pragma once

#include <NimBLEDevice.h>

#include <string>

#include "camera/Camera.h"

class GoProCamera : public Camera {
   public:
    // Connect by MAC if given, else scan for the first name starting with namePrefix
    explicit GoProCamera(const char* macAddress, const char* namePrefix = "GoPro");

    bool begin() override;
    void poll() override;
    bool startRecord() override;
    bool stopRecord() override;
    bool takePhoto() override;

    bool supportsPresets() const override {
        return true;
    }
    bool loadPresetGroup(PresetGroup g) override;
    bool loadPreset(uint32_t presetId) override;
    bool setMode(CamMode mode) override {  // photo/video via the matching preset group
        return loadPresetGroup(mode == CamMode::Photo ? PresetGroup::Photo : PresetGroup::Video);
    }
    bool setDateTime(const WallClock& utc, int16_t tzOffsetMinutes) override;

    const CameraStatus& status() const override {
        return status_;
    }
    bool isConnected() const override {
        return connected_;
    }

   private:
    bool attempt();  // resolve address (once) then connect
    bool resolveAddress();
    bool connect();

    bool writeCmd(const uint8_t* tlv, size_t len);
    bool writeQuery(const uint8_t* tlv, size_t len);
    bool writeSetting(const uint8_t* tlv, size_t len);

    void requestStatuses();
    void requestSettings();  // poll resolution/fps (Get Setting Values, query 0x12)
    void keepAlive();

    void onNotify(const NimBLEUUID& uuid, const uint8_t* data, size_t len);
    void handleQueryResponse(const uint8_t* tlv, size_t len);

    class ClientCB;
    class Reassembler;

    std::string   mac_;
    std::string   namePrefix_;
    NimBLEAddress addr_;
    bool          haveAddr_ = false;

    NimBLEClient*               client_    = nullptr;
    NimBLERemoteCharacteristic* chCommand_ = nullptr;  // 0072
    NimBLERemoteCharacteristic* chSetting_ = nullptr;  // 0074
    NimBLERemoteCharacteristic* chQuery_   = nullptr;  // 0076
    ClientCB*                   cb_        = nullptr;
    Reassembler*                rxQuery_   = nullptr;  // query resp (0077)

    CameraStatus  status_;
    volatile bool connected_     = false;
    uint32_t      lastAttemptMs_ = 0;
    uint32_t      connectedAtMs_ = 0;
    uint32_t      lastPollMs_    = 0;
    uint32_t      lastKeepMs_    = 0;
};
