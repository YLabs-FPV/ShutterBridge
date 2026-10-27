#pragma once

#include <cstdint>

// Video resolution / frame rate enums shared by the DJI R-SDK status push and the
// DUML camera parameters. Indices, not raw values; "" / 0 = unknown code
namespace dji {

    inline const char* resolutionName(uint8_t idx) {
        switch (idx) {
            case 10:
                return "1080P";
            case 12:
                return "1080P 4:3";
            case 16:
                return "4K";
            case 45:
                return "2.7K";
            case 66:
                return "1080P 9:16";
            case 67:
                return "2.7K 9:16";
            case 95:
                return "2.7K 4:3";
            case 103:
                return "4K 4:3";
            case 105:
                return "1080P 1:1";
            case 106:
                return "2160P 1:1";
            case 107:
                return "3K 1:1";
            case 108:
                return "3K 9:16";
            case 109:
                return "4K 9:16";
            case 125:
                return "4K OPEN";
            default:
                return "";
        }
    }

    inline uint16_t fpsValue(uint8_t idx) {
        switch (idx) {
            case 1:
                return 24;
            case 2:
                return 25;
            case 3:
                return 30;
            case 4:
                return 48;
            case 5:
                return 50;
            case 6:
                return 60;
            case 7:
                return 120;
            case 8:
                return 240;
            case 10:
                return 100;
            case 11:
                return 96;
            case 19:
                return 200;
            case 29:
                return 15;
            default:
                return 0;
        }
    }

}  // namespace dji
