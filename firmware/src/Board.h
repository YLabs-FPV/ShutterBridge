// Board variants that share one firmware image, told apart at runtime
#pragma once

#include <cstdint>

enum class BoardVariant : uint8_t {
    Auto = 0,  // settings value only: detect from the chip
    Default,   // the build's own board (BOARD_ID)
    StampS3A,  // M5Stack Stamp-S3A: RGB LED rail switched by GPIO38, GRB LED
    _Count,
};

// True when this build can run on more than one variant (the web UI shows the selector)
bool boardHasVariants();

// Resolves the saved setting (Auto -> chip detection) and powers board peripherals.
// Call once at boot, before the status LED starts
BoardVariant boardInit(uint8_t setting);

BoardVariant boardVariant();
bool         boardLedGrb();  // status LED takes G, R, B byte order
const char*  boardId();
