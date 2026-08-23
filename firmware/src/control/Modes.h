#pragma once

#include <cstdint>

enum class Func : uint8_t {
    Shutter,          // mode-aware trigger: PHOTO mode -> snap; VIDEO mode -> record
    CameraMode,       // switch camera photo<->video (Action/GoPro; Osmo Nano can't over BLE)
    PresetVideo,      // load the Video preset group (GoPro only)
    PresetPhoto,      // load the Photo preset group (GoPro only)
    PresetTimelapse,  // load the Timelapse preset group (GoPro only)
    Count,
};
constexpr int FUNC_COUNT = (int)Func::Count;

enum class ShutterVideoMode : uint8_t {
    Momentary = 0,
    TwoPos    = 1,
};

enum class OpMode : uint8_t {
    Manual      = 0,
    RecordOnArm = 1,
};

struct ModeRange {
    uint8_t  aux      = 0;  // 0 = Disabled, else AUX number (1..14)
    uint16_t rangeMin = 1800;
    uint16_t rangeMax = 2100;
};

inline const char* funcName(Func f) {
    switch (f) {
        case Func::Shutter:
            return "Shutter";
        case Func::CameraMode:
            return "Camera Mode";
        case Func::PresetVideo:
            return "Preset Video";
        case Func::PresetPhoto:
            return "Preset Photo";
        case Func::PresetTimelapse:
            return "Preset Timelapse";
        default:
            return "?";
    }
}

// GoPro-only preset functions
inline bool funcIsPreset(Func f) {
    return f == Func::PresetVideo || f == Func::PresetPhoto || f == Func::PresetTimelapse;
}

inline bool funcMomentary(Func f) {
    return funcIsPreset(f);
}
