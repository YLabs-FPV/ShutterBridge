#include "Board.h"

#include <Arduino.h>

#include "config.h"

#if CONFIG_IDF_TARGET_ESP32S3
#include <esp_efuse.h>
#include <esp_efuse_table.h>

#define STAMP_S3A_LED_POWER_PIN 38

// In-package memory from eFuse: FLASH_CAP 1 = 8 MB, PSRAM_CAP 0 = none.
// The Stamp-S3A's ESP32-S3FN8 matches; the S3-Zero's ESP32-S3FH4R2 (4 MB + 2 MB PSRAM) does not
static BoardVariant detect() {
    uint8_t flashCap = 0, psramCap = 0;
    esp_efuse_read_field_blob(ESP_EFUSE_FLASH_CAP, &flashCap, 3);
    esp_efuse_read_field_blob(ESP_EFUSE_PSRAM_CAP, &psramCap, 2);
    Serial.printf("[board] eFuse flash_cap=%u psram_cap=%u\n", flashCap, psramCap);
    return flashCap == 1 && psramCap == 0 ? BoardVariant::StampS3A : BoardVariant::Default;
}
#else
static BoardVariant detect() {
    return BoardVariant::Default;
}
#endif

static BoardVariant s_variant = BoardVariant::Default;

bool boardHasVariants() {
#if CONFIG_IDF_TARGET_ESP32S3
    return true;
#else
    return false;
#endif
}

BoardVariant boardInit(uint8_t setting) {
    if (!boardHasVariants())
        s_variant = BoardVariant::Default;
    else if (setting == (uint8_t)BoardVariant::Auto || setting >= (uint8_t)BoardVariant::_Count)
        s_variant = detect();
    else
        s_variant = (BoardVariant)setting;

#ifdef STAMP_S3A_LED_POWER_PIN
    if (s_variant == BoardVariant::StampS3A) {
        pinMode(STAMP_S3A_LED_POWER_PIN, OUTPUT);
        digitalWrite(STAMP_S3A_LED_POWER_PIN, HIGH);
    }
#endif

    Serial.printf("[board] %s%s\n", boardId(),
                  setting == (uint8_t)BoardVariant::Auto ? " (auto)" : "");
    return s_variant;
}

BoardVariant boardVariant() {
    return s_variant;
}

bool boardLedGrb() {
    return s_variant == BoardVariant::StampS3A;
}

const char* boardId() {
    return s_variant == BoardVariant::StampS3A ? "m5stamp-s3a" : BOARD_ID;
}

// Leaves out flash/PSRAM, native USB and boot-critical strapping pins
#if CONFIG_IDF_TARGET_ESP32S3
static const uint8_t UART_PINS[] = {1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14,
                                    15, 16, 17, 18, 21, 38, 39, 40, 41, 42, 43, 44, 47, 48};
#else
static const uint8_t UART_PINS[] = {0, 1, 2, 3, 4, 5, 6, 7, 10, 20, 21};
#endif

static bool pinReserved(uint8_t pin) {
    if (pin == STATUS_LED_PIN)
        return true;
#ifdef STAMP_S3A_LED_POWER_PIN
    if (s_variant == BoardVariant::StampS3A && pin == STAMP_S3A_LED_POWER_PIN)
        return true;
#endif
    return false;
}

size_t boardUartPins(uint8_t* out, size_t max) {
    size_t n = 0;
    for (uint8_t pin : UART_PINS)
        if (!pinReserved(pin) && n < max)
            out[n++] = pin;
    return n;
}

static bool pinOffered(int pin) {
    for (uint8_t p : UART_PINS)
        if (p == pin)
            return !pinReserved(p);
    return false;
}

void boardFcPins(int8_t rxSetting, int8_t txSetting, int& rx, int& tx) {
    rx = rxSetting < 0 ? FC_RX_PIN : rxSetting;
    tx = txSetting < 0 ? FC_TX_PIN : txSetting;
    if (rx == tx || !pinOffered(rx) || !pinOffered(tx)) {
        rx = FC_RX_PIN;
        tx = FC_TX_PIN;
    }
}
