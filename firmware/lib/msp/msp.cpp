#include "msp.h"

namespace msp {

    uint8_t crc8DvbS2(uint8_t crc, uint8_t b) {
        crc ^= b;
        for (int i = 0; i < 8; i++)
            crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0xD5) : (uint8_t)(crc << 1);
        return crc;
    }

    std::vector<uint8_t> encodeV1(uint8_t cmd, const uint8_t* payload, size_t len) {
        std::vector<uint8_t> f;
        f.reserve(6 + len);
        f.push_back('$');
        f.push_back('M');
        f.push_back('<');
        f.push_back((uint8_t)len);
        f.push_back(cmd);
        for (size_t i = 0; i < len; i++)
            f.push_back(payload[i]);
        uint8_t ck = 0;  // XOR over size, cmd, payload
        for (size_t i = 3; i < f.size(); i++)
            ck ^= f[i];
        f.push_back(ck);
        return f;
    }

    std::vector<uint8_t> encodeV2(uint16_t cmd, const uint8_t* payload, size_t len) {
        std::vector<uint8_t> f;
        f.reserve(9 + len);
        f.push_back('$');
        f.push_back('X');
        f.push_back('<');
        f.push_back(0);  // flags
        f.push_back((uint8_t)(cmd & 0xFF));
        f.push_back((uint8_t)((cmd >> 8) & 0xFF));
        f.push_back((uint8_t)(len & 0xFF));
        f.push_back((uint8_t)((len >> 8) & 0xFF));
        for (size_t i = 0; i < len; i++)
            f.push_back(payload[i]);
        uint8_t crc = 0;  // DVB-S2 over flags, cmd, size, payload
        for (size_t i = 3; i < f.size(); i++)
            crc = crc8DvbS2(crc, f[i]);
        f.push_back(crc);
        return f;
    }

    void Parser::feed(const uint8_t* data, size_t n, const Handler& handler) {
        for (size_t i = 0; i < n; i++)
            step(data[i], handler);
    }

    void Parser::step(uint8_t c, const Handler& handler) {
        switch (st_) {
            case S_DOLLAR:
                if (c == '$')
                    st_ = S_MAGIC;
                break;
            case S_MAGIC:
                if (c == 'M') {
                    v2_ = false;
                    st_ = S_DIR;
                } else if (c == 'X') {
                    v2_ = true;
                    st_ = S_DIR;
                } else
                    st_ = (c == '$') ? S_MAGIC : S_DOLLAR;
                break;
            case S_DIR:
                err_ = (c == '!');
                if (c == '>' || c == '!') {
                    data_.clear();
                    if (v2_) {
                        ck_ = 0;
                        st_ = V2_FLAG;
                    } else {
                        ck_ = 0;
                        st_ = V1_SIZE;
                    }
                } else {
                    st_ = (c == '$') ? S_MAGIC : S_DOLLAR;
                }
                break;

            // ---- MSP v1 ----
            case V1_SIZE:
                size_ = c;
                ck_ ^= c;
                idx_ = 0;
                st_  = V1_CMD;
                break;
            case V1_CMD:
                cmd_ = c;
                ck_ ^= c;
                st_ = size_ ? V1_DATA : V1_CRC;
                break;
            case V1_DATA:
                data_.push_back(c);
                ck_ ^= c;
                if (++idx_ >= size_)
                    st_ = V1_CRC;
                break;
            case V1_CRC:
                if (c == ck_) {
                    Message m{false, err_, cmd_, data_.data(), data_.size()};
                    handler(m);
                }
                st_ = S_DOLLAR;
                break;

            // ---- MSP v2 ----
            case V2_FLAG:
                ck_ = crc8DvbS2(ck_, c);
                st_ = V2_CMDL;
                break;
            case V2_CMDL:
                cmd_ = c;
                ck_  = crc8DvbS2(ck_, c);
                st_  = V2_CMDH;
                break;
            case V2_CMDH:
                cmd_ |= (uint16_t)c << 8;
                ck_ = crc8DvbS2(ck_, c);
                st_ = V2_SZL;
                break;
            case V2_SZL:
                size_ = c;
                ck_   = crc8DvbS2(ck_, c);
                st_   = V2_SZH;
                break;
            case V2_SZH:
                size_ |= (uint16_t)c << 8;
                ck_  = crc8DvbS2(ck_, c);
                idx_ = 0;
                if (size_ > 512) {
                    st_ = S_DOLLAR;
                    break;
                }  // noise guard
                st_ = size_ ? V2_DATA : V2_CRC;
                break;
            case V2_DATA:
                data_.push_back(c);
                ck_ = crc8DvbS2(ck_, c);
                if (++idx_ >= size_)
                    st_ = V2_CRC;
                break;
            case V2_CRC:
                if (c == ck_) {
                    Message m{true, err_, cmd_, data_.data(), data_.size()};
                    handler(m);
                }
                st_ = S_DOLLAR;
                break;
        }
    }

}  // namespace msp
