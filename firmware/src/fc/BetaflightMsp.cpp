#include "fc/BetaflightMsp.h"

#include <string.h>

BetaflightMsp::BetaflightMsp(HardwareSerial& serial, uint32_t baud, int rxPin, int txPin,
                             uint32_t rcPollMs)
    : serial_(serial), baud_(baud), rxPin_(rxPin), txPin_(txPin), rcPollMs_(rcPollMs) {
}

void BetaflightMsp::begin() {
    serial_.begin(baud_, SERIAL_8N1, rxPin_, txPin_);
}

void BetaflightMsp::poll() {
    const uint32_t now = millis();

    // Drain RX and parse any complete reply frames
    while (serial_.available()) {
        const int c = serial_.read();
        if (c < 0)
            break;
        const uint8_t b = (uint8_t)c;
        parser_.feed(&b, 1, [this](const msp::Message& m) { onMessage(m); });
    }

    // Periodically ask for RC channels
    if (now - lastReqMs_ >= rcPollMs_) {
        lastReqMs_ = now;
        requestRc();
    }

    // Poll the RTC quickly until we have a time, then only occasionally
    const uint32_t rtcInterval = time_.valid ? 30000 : 3000;
    if (now - lastRtcReqMs_ >= rtcInterval) {
        lastRtcReqMs_ = now;
        requestRtc();
    }

    // ARM state: locate ARM's flag bit once (MSP_BOXIDS), then poll MSP_STATUS
    if (armBit_ < 0 && now - lastBoxIdsMs_ >= 2000) {
        lastBoxIdsMs_ = now;
        requestBoxIds();
    }
    if (armBit_ >= 0 && now - lastStatusMs_ >= 200) {
        lastStatusMs_ = now;
        requestStatus();
    }
}

void BetaflightMsp::requestRc() {
    auto f = msp::encodeV1(msp::MSP_RC);
    serial_.write(f.data(), f.size());
}

void BetaflightMsp::requestRtc() {
    auto f = msp::encodeV1(msp::MSP_RTC);
    serial_.write(f.data(), f.size());
}

void BetaflightMsp::requestStatus() {
    auto f = msp::encodeV1(msp::MSP_STATUS);
    serial_.write(f.data(), f.size());
}

void BetaflightMsp::requestBoxIds() {
    auto f = msp::encodeV1(msp::MSP_BOXIDS);
    serial_.write(f.data(), f.size());
}

void BetaflightMsp::onMessage(const msp::Message& m) {
    if (m.error)
        return;
    if (m.cmd == msp::MSP_RC) {
        const uint8_t n = (uint8_t)(m.len / 2);
        rc_.count       = n > RcState::MAX ? RcState::MAX : n;
        for (int i = 0; i < rc_.count; i++) {
            rc_.ch[i] = (uint16_t)m.payload[2 * i] | ((uint16_t)m.payload[2 * i + 1] << 8);
        }
        rc_.valid        = true;
        rc_.lastUpdateMs = millis();
    } else if (m.cmd == msp::MSP_RTC && m.len >= 9) {
        // year u16, month, day, hours, minutes, seconds, millis u16 (UTC). Empty until set
        time_.year    = (uint16_t)m.payload[0] | ((uint16_t)m.payload[1] << 8);
        time_.month   = m.payload[2];
        time_.day     = m.payload[3];
        time_.hours   = m.payload[4];
        time_.minutes = m.payload[5];
        time_.seconds = m.payload[6];
        time_.valid   = time_.year >= 2020;
    } else if (m.cmd == msp::MSP_BOXIDS) {
        // Permanent IDs in flag-bit order; BOXARM = 0, so its position is ARM's bit
        for (size_t i = 0; i < m.len && i < 32; i++) {
            if (m.payload[i] == 0) {
                armBit_ = (int8_t)i;
                break;
            }
        }
    } else if (m.cmd == msp::MSP_STATUS && armBit_ >= 0 && m.len >= 10) {
        const uint32_t flags = (uint32_t)m.payload[6] | ((uint32_t)m.payload[7] << 8) |
                               ((uint32_t)m.payload[8] << 16) | ((uint32_t)m.payload[9] << 24);
        armed_               = (flags >> armBit_) & 1;
        armedKnown_          = true;
    }
}

void BetaflightMsp::setOsdMessage(uint8_t slot, const char* text) {
    if (slot > 3)
        return;
    uint8_t len = (uint8_t)strlen(text);
    if (len > 30)
        len = 30;

    uint8_t payload[2 + 30];
    payload[0] = (uint8_t)(msp::MSP2TEXT_CUSTOM_MSG_0 + slot);
    payload[1] = len;
    memcpy(&payload[2], text, len);

    auto f = msp::encodeV2(msp::MSP2_SET_TEXT, payload, 2 + len);
    serial_.write(f.data(), f.size());
}
