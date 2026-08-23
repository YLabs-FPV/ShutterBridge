#pragma once

#include <cstdint>

struct RcState {
    static constexpr int MAX          = 18;
    uint16_t             ch[MAX]      = {0};
    uint8_t              count        = 0;
    bool                 valid        = false;
    uint32_t             lastUpdateMs = 0;

    // AUX number is 1-based: AUX1 -> index 4
    uint16_t aux(uint8_t auxNumber) const {
        const int i = 4 + (int)auxNumber - 1;
        return (i >= 0 && i < count) ? ch[i] : 0;
    }
    bool isFresh(uint32_t nowMs, uint32_t maxAgeMs = 500) const {
        return valid && (nowMs - lastUpdateMs) <= maxAgeMs;
    }
};

// UTC wall clock from the FC's RTC (populated from GPS)
struct FcTime {
    bool     valid   = false;
    uint16_t year    = 0;
    uint8_t  month   = 0;
    uint8_t  day     = 0;
    uint8_t  hours   = 0;
    uint8_t  minutes = 0;
    uint8_t  seconds = 0;
};

class FlightController {
   public:
    virtual ~FlightController()                                          = default;
    virtual void           begin()                                       = 0;
    virtual void           poll()                                        = 0;
    virtual const RcState& rc() const                                    = 0;
    virtual const FcTime&  time() const                                  = 0;
    virtual bool           armed() const                                 = 0;
    virtual bool           armedKnown() const                            = 0;
    virtual void           setOsdMessage(uint8_t slot, const char* text) = 0;
};
