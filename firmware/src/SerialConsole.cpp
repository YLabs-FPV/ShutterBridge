#include "SerialConsole.h"

#include <Arduino.h>

#include <cstring>

#include "config.h"
#include "config/Settings.h"

void SerialConsole::printInfo() {
    Serial.printf("sb:info {\"fw\":\"%s\",\"schema\":%u,\"board\":\"%s\"}\n", FW_VERSION,
                  (unsigned)Settings::SCHEMA, BOARD_ID);
}

void SerialConsole::poll() {
    while (Serial.available() > 0) {
        const int c = Serial.read();
        if (c < 0)
            break;
        if (c == '\r' || c == '\n') {
            if (!overflow_ && len_ > 0) {
                buf_[len_] = '\0';
                handle(buf_);
            }
            len_      = 0;
            overflow_ = false;
        } else if (len_ < sizeof(buf_) - 1) {
            buf_[len_++] = (char)c;
        } else {
            overflow_ = true;  // drop the whole line rather than act on a truncated one
        }
    }
}

void SerialConsole::handle(const char* line) {
    if (strcmp(line, "sb info") == 0)
        printInfo();
}
