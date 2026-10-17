#pragma once

#define FW_VERSION "0.2.0"

#if defined(BOARD_ESP32C3_SUPERMINI)
// ESP32-C3 Super Mini: FC UART on 20/21, single-color LED on GPIO8
#define FC_RX_PIN             20
#define FC_TX_PIN             21
#define STATUS_LED_PIN        8
#define STATUS_LED_RGB        false
#define STATUS_LED_ACTIVE_LOW true
#else
// Waveshare ESP32-S3-Zero: FC UART on GPIO43/44, WS2812 RGB LED on GPIO21
#define FC_RX_PIN             44
#define FC_TX_PIN             43
#define STATUS_LED_PIN        21
#define STATUS_LED_RGB        true
#define STATUS_LED_ACTIVE_LOW false
#endif

#define FC_UART       Serial1
#define FC_BAUD       115200
#define FC_RC_POLL_MS 50

#define OSD_UPDATE_MS  250
#define OSD_REFRESH_MS 1000

#define CONSOLE_BAUD 115200
