#pragma once

#include <NimBLEDevice.h>

#include "config/Settings.h"

inline void applyBleTxPower() {
    esp_power_level_t lvl;
    switch (g_settings.bleTxPower) {
        case 0:
            lvl = ESP_PWR_LVL_N9;
            break;
        case 1:
            lvl = ESP_PWR_LVL_P3;
            break;
        default:
            lvl = ESP_PWR_LVL_P9;
            break;
    }
    NimBLEDevice::setPower(lvl);
}
