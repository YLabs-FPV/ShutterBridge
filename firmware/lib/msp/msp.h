#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

namespace msp {

    // MSP command IDs (Betaflight msp_protocol*.h)
    enum : uint16_t {
        MSP_STATUS    = 101,     // v1: flight mode flags (incl. ARM), sensors, etc.
        MSP_RC        = 105,     // v1: channelCount x u16 (RC channel values, us)
        MSP_BOXIDS    = 119,     // v1: permanent box IDs in flight-mode-flag bit order
        MSP_RTC       = 247,     // v1: RTC clock (empty reply until the FC has a time)
        MSP2_SET_TEXT = 0x3007,  // v2: set craft/pilot name & custom OSD messages
    };

    // MSP2_SET_TEXT variable types (msp_protocol_v2_betaflight.h)
    enum : uint8_t {
        MSP2TEXT_CUSTOM_MSG_0 = 7,  // +0..+3 -> OSD "Custom Msg 0..3" elements
    };

    uint8_t crc8DvbS2(uint8_t crc, uint8_t b);

    // Build a request frame ("$M<" / "$X<") ready to write to the FC UART
    std::vector<uint8_t> encodeV1(uint8_t cmd, const uint8_t* payload = nullptr, size_t len = 0);
    std::vector<uint8_t> encodeV2(uint16_t cmd, const uint8_t* payload = nullptr, size_t len = 0);

    struct Message {
        bool           v2      = false;
        bool           error   = false;  // '!' reply
        uint16_t       cmd     = 0;
        const uint8_t* payload = nullptr;
        size_t         len     = 0;
    };

    // Streaming reply parser. Feed received bytes; `handler` runs once per valid
    // frame ("$M>" / "$M!" / "$X>" / "$X!")
    class Parser {
       public:
        using Handler = std::function<void(const Message&)>;
        void feed(const uint8_t* data, size_t n, const Handler& handler);
        void reset() {
            st_ = S_DOLLAR;
            data_.clear();
        }

       private:
        enum State : uint8_t {
            S_DOLLAR,
            S_MAGIC,
            S_DIR,
            V1_SIZE,
            V1_CMD,
            V1_DATA,
            V1_CRC,
            V2_FLAG,
            V2_CMDL,
            V2_CMDH,
            V2_SZL,
            V2_SZH,
            V2_DATA,
            V2_CRC,
        };
        void step(uint8_t c, const Handler& handler);

        State                st_   = S_DOLLAR;
        bool                 v2_   = false;
        bool                 err_  = false;
        uint16_t             cmd_  = 0;
        uint16_t             size_ = 0;
        uint16_t             idx_  = 0;
        uint8_t              ck_   = 0;
        std::vector<uint8_t> data_;
    };

}  // namespace msp
