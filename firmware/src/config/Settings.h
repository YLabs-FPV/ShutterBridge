// User-editable config, persisted in NVS (edited via the Web UI)
#pragma once

#include <cstdint>

#include "control/Modes.h"

// What a given OSD "Custom Msg" slot displays
enum class OsdField : uint8_t {
    Off = 0,
    RecStatus,
    Battery,
    Mode,
    SdFree,
    TimeLeft,
    Resolution,
    Fps,
    _Count,
};

const char* osdFieldName(OsdField f);

struct Settings {
    uint32_t magic   = 0x53425247;  // 'SBRG'
    uint16_t version = 15;

    uint8_t opMode = (uint8_t)OpMode::RecordOnArm;

    // --- Wi-Fi config AP ---
    char    apSsid[32]        = "ShutterBridge";
    char    apPass[32]        = "shutterbridge";
    uint8_t webuiDisarmedOnly = 1;

    // BLE transmit power: 0 = low, 1 = medium, 2 = high (max)
    uint8_t bleTxPower = 0;

    // --- Camera selection ---
    uint8_t camType           = 0;   // 0 = DJI Osmo Series, 1 = DJI Action Series, 2 = GoPro
    char    camMac[18]        = "";  // bound via Web UI scan; "" falls back to name scan
    char    camNamePrefix[16] = "OsmoNano";

    // --- OSD: which field each of the 4 Custom Msg slots shows ---
    OsdField osdSlot[4] = {OsdField::RecStatus, OsdField::Battery, OsdField::Mode,
                           OsdField::SdFree};

    // --- Camera modes: AUX channel + active range per function (Modes.h) ---
    ModeRange modes[FUNC_COUNT] = {};

    // How the Shutter behaves in video mode (0 = Momentary/toggle, 1 = 2-position/hold)
    uint8_t shutterVideoMode = (uint8_t)ShutterVideoMode::TwoPos;

    // How the Camera Mode switch behaves
    uint8_t modeSwitchStyle = (uint8_t)ShutterVideoMode::TwoPos;

    // Delays (ms) applied to the Shutter's record commands
    uint16_t recordStartDelayMs = 0;
    uint16_t recordStopDelayMs  = 2000;

    // Sync the camera clock from FC GPS time (1 = on)
    uint8_t clockSync   = 1;
    int16_t tzOffsetMin = 0;

    void setDefaults();
    void load();  // from NVS (falls back to defaults if missing/incompatible)
    void save();  // to NVS
};

extern Settings g_settings;
