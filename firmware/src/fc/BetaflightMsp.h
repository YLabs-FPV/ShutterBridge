#pragma once

#include <Arduino.h>

#include "fc/FlightController.h"
#include "msp.h"

class BetaflightMsp : public FlightController {
   public:
    BetaflightMsp(HardwareSerial& serial, uint32_t baud, int rxPin, int txPin,
                  uint32_t rcPollMs = 50);

    void           begin() override;
    void           poll() override;
    const RcState& rc() const override {
        return rc_;
    }
    const FcTime& time() const override {
        return time_;
    }
    bool armed() const override {
        return armed_;
    }
    bool armedKnown() const override {
        return armedKnown_;
    }
    void setOsdMessage(uint8_t slot, const char* text) override;

   private:
    void requestRc();
    void requestRtc();
    void requestStatus();
    void requestBoxIds();
    void onMessage(const msp::Message& m);

    HardwareSerial& serial_;
    uint32_t        baud_;
    int             rxPin_;
    int             txPin_;
    uint32_t        rcPollMs_;

    msp::Parser parser_;
    RcState     rc_;
    FcTime      time_;
    bool        armed_        = false;
    bool        armedKnown_   = false;
    int8_t      armBit_       = -1;  // ARM's bit in flightModeFlags; -1 until MSP_BOXIDS parsed
    uint32_t    lastReqMs_    = 0;
    uint32_t    lastRtcReqMs_ = 0;
    uint32_t    lastStatusMs_ = 0;
    uint32_t    lastBoxIdsMs_ = 0;
};
