// Board variants that share one firmware image, told apart at runtime
#pragma once

#include <cstddef>
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

// GPIOs safe to offer for the FC UART on this chip and variant (call after boardInit)
size_t boardUartPins(uint8_t* out, size_t max);

// Saved FC UART pins (-1 = default) resolved to GPIOs; falls back to the build's pins
// when either is not offered or both are the same
void boardFcPins(int8_t rxSetting, int8_t txSetting, int& rx, int& tx);
