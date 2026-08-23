#pragma once

#include <cstdint>

enum class CamMode : uint8_t {
    Photo   = 0,
    Video   = 1,
    Unknown = 0xFF,
};

enum class RecState : uint8_t {
    Idle      = 0,
    Starting  = 1,
    Recording = 2,
    Stopping  = 3,
};

// GoPro preset groups; cameras without presets ignore these
enum class PresetGroup : uint8_t {
    Video     = 0,
    Photo     = 1,
    Timelapse = 2,
};

struct CameraStatus {
    bool     connected = false;
    RecState recState  = RecState::Idle;
    CamMode  mode      = CamMode::Unknown;
    bool     ready     = true;
    // Decoded per-backend; "" / 0 = unreported (the Osmo Nano doesn't expose these)
    char     resolution[12] = "";
    uint16_t fps            = 0;
    uint32_t recElapsedS    = 0;
    uint32_t recLeftS       = 0;
    uint32_t sdFreeMB       = 0;
    uint8_t  batteryPct     = 0;
    uint16_t batteryMv      = 0;
    uint32_t lastUpdateMs   = 0;
    bool     paired         = true;

    bool isRecording() const {
        return recState == RecState::Recording || recState == RecState::Starting;
    }
};

class Camera {
   public:
    virtual ~Camera()          = default;
    virtual bool begin()       = 0;
    virtual void poll()        = 0;
    virtual bool startRecord() = 0;
    virtual bool stopRecord()  = 0;
    virtual bool takePhoto()   = 0;

    // Preset switching (GoPro only)
    virtual bool supportsPresets() const {
        return false;
    }
    virtual bool loadPresetGroup(PresetGroup) {
        return false;
    }
    virtual bool loadPreset(uint32_t /*presetId*/) {
        return false;
    }

    // Switch the camera between photo and video. Backends that can't (e.g. Osmo Nano)
    // keep this no-op default and return false
    virtual bool setMode(CamMode /*mode*/) {
        return false;
    }

    // Set the camera clock. utc is the FC's UTC time; tzOffsetMinutes shifts it to local
    // Default: unsupported
    struct WallClock {
        uint16_t year;
        uint8_t  month, day, hour, minute, second;
    };
    virtual bool setDateTime(const WallClock& /*utc*/, int16_t /*tzOffsetMinutes*/) {
        return false;
    }

    virtual const CameraStatus& status() const      = 0;
    virtual bool                isConnected() const = 0;
};

inline const char* toString(RecState s) {
    switch (s) {
        case RecState::Idle:
            return "IDLE";
        case RecState::Starting:
            return "START";
        case RecState::Recording:
            return "REC";
        case RecState::Stopping:
            return "STOP";
    }
    return "?";
}

inline const char* toString(CamMode m) {
    switch (m) {
        case CamMode::Photo:
            return "photo";
        case CamMode::Video:
            return "video";
        default:
            return "?";
    }
}
