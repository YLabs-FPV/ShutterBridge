#include "camera/gopro/GoProCamera.h"

#include <Arduino.h>

#include <cstring>

#include "camera/BleTxPower.h"

namespace {
    const NimBLEUUID UUID_SERVICE((uint16_t)0xFEA6);  // Control & Query

    NimBLEUUID gp(const char* four) {
        return NimBLEUUID(std::string("b5f9") + four + "-aa8d-11e3-9046-0002a5d5c51b");
    }

    constexpr uint8_t CMD_SET_SHUTTER       = 0x01;
    constexpr uint8_t CMD_SET_DATETIME      = 0x0D;
    constexpr uint8_t CMD_LOAD_PRESET_GROUP = 0x3E;
    constexpr uint8_t CMD_LOAD_PRESET       = 0x40;
    constexpr uint8_t QUERY_GET_STATUS_VAL  = 0x13;
    constexpr uint8_t QUERY_GET_SETTING_VAL = 0x12;
    constexpr uint8_t SETTING_KEEP_ALIVE    = 0x5B;

    constexpr uint8_t SET_RESOLUTION = 2;
    constexpr uint8_t SET_FPS        = 3;

    constexpr uint8_t ST_BUSY         = 8;
    constexpr uint8_t ST_ENCODING     = 10;
    constexpr uint8_t ST_ENC_DURATION = 13;
    constexpr uint8_t ST_REMAIN_VIDEO = 35;
    constexpr uint8_t ST_SD_REMAIN    = 54;  // KB
    constexpr uint8_t ST_BATTERY_PCT  = 70;

    constexpr uint16_t GRP_VIDEO     = 1000;
    constexpr uint16_t GRP_PHOTO     = 1001;
    constexpr uint16_t GRP_TIMELAPSE = 1002;

    uint32_t beU(const uint8_t* p, size_t n) {
        uint32_t v = 0;
        for (size_t i = 0; i < n; i++)
            v = (v << 8) | p[i];
        return v;
    }

    // Civil-date <-> days-since-epoch (Howard Hinnant's algorithm), for the UTC->local shift.
    long daysFromCivil(int y, unsigned m, unsigned d) {
        y -= m <= 2;
        const long     era = (y >= 0 ? y : y - 399) / 400;
        const unsigned yoe = (unsigned)(y - era * 400);
        const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
        const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
        return era * 146097L + (long)doe - 719468;
    }
    void civilFromDays(long z, int& y, unsigned& m, unsigned& d) {
        z += 719468;
        const long     era = (z >= 0 ? z : z - 146096) / 146097;
        const unsigned doe = (unsigned)(z - era * 146097);
        const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
        const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
        const unsigned mp  = (5 * doy + 2) / 153;
        d                  = doy - (153 * mp + 2) / 5 + 1;
        m                  = mp + (mp < 10 ? 3 : -9);
        y                  = (int)yoe + (int)(era * 400) + (m <= 2);
    }

    const char* goproResolution(uint8_t v) {
        switch (v) {
            case 1:
                return "4K";
            case 4:
                return "2.7K";
            case 6:
                return "2.7K 4:3";
            case 7:
                return "1440";
            case 9:
                return "1080";
            case 12:
                return "720";
            case 18:
                return "4K 4:3";
            case 21:
                return "5.6K";
            case 24:
                return "5K";
            case 25:
                return "5K 4:3";
            case 26:
            case 107:
                return "5.3K 8:7";
            case 27:
            case 113:
                return "5.3K 4:3";
            case 28:
            case 108:
                return "4K 8:7";
            case 31:
                return "8K";
            case 35:
                return "5.3K 21:9";
            case 36:
                return "4K 21:9";
            case 37:
                return "4K 1:1";
            case 38:
                return "900";
            case 39:
                return "4K SPH";
            case 100:
                return "5.3K";
            case 109:
                return "4K 9:16";
            case 110:
                return "1080 9:16";
            case 111:
                return "2.7K 4:3";
            case 112:
                return "4K 4:3";
            default:
                return "";
        }
    }

    uint16_t goproFps(uint8_t v) {
        switch (v) {
            case 0:
                return 240;
            case 1:
                return 120;
            case 2:
                return 100;
            case 3:
                return 90;
            case 5:
                return 60;
            case 6:
                return 50;
            case 8:
                return 30;
            case 9:
                return 25;
            case 10:
                return 24;
            case 13:
                return 200;
            case 15:
                return 400;
            case 16:
                return 360;
            case 17:
                return 300;
            default:
                return 0;
        }
    }
}  // namespace

// Reassembles multi-packet responses: a start packet carries the total length in a 1/2/3-byte
// header; continuations set bit 7
class GoProCamera::Reassembler {
   public:
    bool feed(const uint8_t* data, size_t len, const uint8_t** msg, size_t* msgLen) {
        if (len == 0)
            return false;
        size_t i = 0;
        if (data[0] & 0x80) {
            i = 1;
        } else {
            buf_.clear();
            const uint8_t hdr = (data[0] & 0x60) >> 5;
            if (hdr == 0) {
                remaining_ = data[0] & 0x1F;
                i          = 1;
            } else if (hdr == 1) {
                if (len < 2)
                    return false;
                remaining_ = ((data[0] & 0x1F) << 8) | data[1];
                i          = 2;
            } else {
                if (len < 3)
                    return false;
                remaining_ = ((uint16_t)data[1] << 8) | data[2];
                i          = 3;
            }
        }
        for (; i < len && remaining_ > 0; i++) {
            buf_.push_back(data[i]);
            remaining_--;
        }
        if (remaining_ == 0 && !buf_.empty()) {
            *msg    = buf_.data();
            *msgLen = buf_.size();
            return true;
        }
        return false;
    }

   private:
    std::vector<uint8_t> buf_;
    size_t               remaining_ = 0;
};

class GoProCamera::ClientCB : public NimBLEClientCallbacks {
   public:
    explicit ClientCB(GoProCamera* owner) : owner_(owner) {
    }
    void onDisconnect(NimBLEClient*, int reason) override {
        owner_->connected_ = false;
        owner_->status_    = CameraStatus{};
        Serial.printf("[gopro] disconnected (reason 0x%02x)\n", reason);
    }

   private:
    GoProCamera* owner_;
};

GoProCamera::GoProCamera(const char* macAddress, const char* namePrefix)
    : mac_(macAddress ? macAddress : ""),
      namePrefix_(namePrefix && *namePrefix ? namePrefix : "GoPro") {
}

bool GoProCamera::begin() {
    NimBLEDevice::init("ShutterBridge");
    applyBleTxPower();
    // GoPro requires a bonded (just-works) link; the first connect needs "Connect New Device"
    // on the camera
    NimBLEDevice::setSecurityAuth(/*bond=*/true, /*mitm=*/false, /*sc=*/true);
    NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);
    cb_      = new ClientCB(this);
    rxQuery_ = new Reassembler();
    return attempt();
}

bool GoProCamera::attempt() {
    if (!haveAddr_) {
        if (!resolveAddress())
            return false;
        haveAddr_ = true;
    }
    return connect();
}

bool GoProCamera::resolveAddress() {
    if (mac_.empty() && namePrefix_.empty())
        return false;

    Serial.printf("[gopro] scanning to resolve %s ...\n",
                  mac_.empty() ? namePrefix_.c_str() : mac_.c_str());
    NimBLEScan* scan = NimBLEDevice::getScan();
    scan->setActiveScan(true);
    NimBLEScanResults res = scan->getResults(4000, false);
    for (int i = 0; i < res.getCount(); i++) {
        const NimBLEAdvertisedDevice* d   = res.getDevice(i);
        const bool                    hit = !mac_.empty() ? d->getAddress().toString() == mac_
                                                          : d->getName().rfind(namePrefix_, 0) == 0;
        if (hit) {
            addr_ = d->getAddress();
            scan->clearResults();
            Serial.printf("[gopro] found %s @ %s\n", d->getName().c_str(),
                          addr_.toString().c_str());
            return true;
        }
    }
    scan->clearResults();
    return false;
}

bool GoProCamera::connect() {
    Serial.printf("[gopro] connecting to %s ...\n", addr_.toString().c_str());
    if (!client_)
        client_ = NimBLEDevice::createClient();
    client_->setClientCallbacks(cb_, /*deleteCallbacks=*/false);
    client_->setConnectTimeout(3000);

    if (!client_->connect(addr_)) {
        Serial.println("[gopro] connect failed");
        return false;
    }

    NimBLERemoteService* svc = client_->getService(UUID_SERVICE);
    if (!svc) {
        Serial.println("[gopro] control/query service (fea6) not found");
        client_->disconnect();
        return false;
    }
    chCommand_                            = svc->getCharacteristic(gp("0072"));
    chSetting_                            = svc->getCharacteristic(gp("0074"));
    chQuery_                              = svc->getCharacteristic(gp("0076"));
    NimBLERemoteCharacteristic* respCmd   = svc->getCharacteristic(gp("0073"));
    NimBLERemoteCharacteristic* respSet   = svc->getCharacteristic(gp("0075"));
    NimBLERemoteCharacteristic* respQuery = svc->getCharacteristic(gp("0077"));
    if (!chCommand_ || !chSetting_ || !chQuery_ || !respCmd || !respSet || !respQuery) {
        Serial.println("[gopro] required characteristics missing");
        client_->disconnect();
        return false;
    }

    status_ = CameraStatus{};

    respQuery->subscribe(true, [this](NimBLERemoteCharacteristic*, uint8_t* d, size_t n, bool) {
        onNotify(gp("0077"), d, n);
    });
    auto ackCb = [this](NimBLERemoteCharacteristic*, uint8_t* d, size_t n, bool) {
        onNotify(gp("0073"), d, n);
    };
    respCmd->subscribe(true, ackCb);
    respSet->subscribe(true, ackCb);

    connected_        = true;
    status_.connected = true;
    status_.mode      = CamMode::Video;
    connectedAtMs_    = millis();
    lastPollMs_       = 0;
    lastKeepMs_       = millis();
    Serial.println("[gopro] connected");
    requestStatuses();
    requestSettings();
    return true;
}

void GoProCamera::poll() {
    const uint32_t now = millis();

    if (connected_) {
        if (now - lastPollMs_ >= 1000) {
            lastPollMs_ = now;
            requestStatuses();
            requestSettings();
        }
        if (now - lastKeepMs_ >= 3000) {  // keep-alive so the camera doesn't sleep
            lastKeepMs_ = now;
            keepAlive();
        }
        const bool stale       = status_.lastUpdateMs != 0 && (now - status_.lastUpdateMs) > 2500;
        const bool noTelemetry = status_.lastUpdateMs == 0 && (now - connectedAtMs_) > 4000;
        if (stale || noTelemetry) {
            Serial.printf("[gopro] link dead (%s) - dropping\n", stale ? "stale" : "no telemetry");
            if (client_)
                client_->disconnect();
            connected_ = false;
            status_    = CameraStatus{};
        }
    }

    if (!connected_ && now - lastAttemptMs_ > 5000) {
        lastAttemptMs_ = now;
        attempt();
    }
}

bool GoProCamera::writeCmd(const uint8_t* tlv, size_t len) {
    if (!connected_ || !chCommand_)
        return false;
    return chCommand_->writeValue(tlv, len, /*response=*/true);
}
bool GoProCamera::writeQuery(const uint8_t* tlv, size_t len) {
    if (!connected_ || !chQuery_)
        return false;
    return chQuery_->writeValue(tlv, len, /*response=*/true);
}
bool GoProCamera::writeSetting(const uint8_t* tlv, size_t len) {
    if (!connected_ || !chSetting_)
        return false;
    return chSetting_->writeValue(tlv, len, /*response=*/true);
}

bool GoProCamera::startRecord() {
    const uint8_t on[] = {0x03, CMD_SET_SHUTTER, 0x01, 0x01};
    return writeCmd(on, sizeof(on));
}
bool GoProCamera::stopRecord() {
    const uint8_t off[] = {0x03, CMD_SET_SHUTTER, 0x01, 0x00};
    return writeCmd(off, sizeof(off));
}
bool GoProCamera::takePhoto() {
    const uint8_t on[] = {0x03, CMD_SET_SHUTTER, 0x01, 0x01};
    return writeCmd(on, sizeof(on));
}

bool GoProCamera::loadPresetGroup(PresetGroup g) {
    uint16_t id = GRP_VIDEO;
    switch (g) {
        case PresetGroup::Photo:
            id = GRP_PHOTO;
            break;
        case PresetGroup::Timelapse:
            id = GRP_TIMELAPSE;
            break;
        case PresetGroup::Video:
            id = GRP_VIDEO;
            break;
    }
    const uint8_t tlv[] = {0x04, CMD_LOAD_PRESET_GROUP, 0x02, (uint8_t)(id >> 8),
                           (uint8_t)(id & 0xFF)};
    status_.mode        = (g == PresetGroup::Photo) ? CamMode::Photo : CamMode::Video;
    Serial.printf("[gopro] load preset group %u\n", id);
    return writeCmd(tlv, sizeof(tlv));
}

bool GoProCamera::loadPreset(uint32_t presetId) {
    const uint8_t tlv[] = {0x06,
                           CMD_LOAD_PRESET,
                           0x04,
                           (uint8_t)(presetId >> 24),
                           (uint8_t)(presetId >> 16),
                           (uint8_t)(presetId >> 8),
                           (uint8_t)(presetId & 0xFF)};
    Serial.printf("[gopro] load preset 0x%08lx\n", (unsigned long)presetId);
    return writeCmd(tlv, sizeof(tlv));
}

bool GoProCamera::setDateTime(const WallClock& utc, int16_t tzOffsetMinutes) {
    long secs =
        ((daysFromCivil(utc.year, utc.month, utc.day) * 24L + utc.hour) * 60L + utc.minute) * 60L +
        utc.second;
    secs += (long)tzOffsetMinutes * 60L;  // shift UTC -> local
    long z = secs / 86400, rem = secs % 86400;
    if (rem < 0) {
        rem += 86400;
        z--;
    }
    int      y;
    unsigned mo, d;
    civilFromDays(z, y, mo, d);
    const uint8_t hh = (uint8_t)(rem / 3600), mi = (uint8_t)((rem % 3600) / 60),
                  ss    = (uint8_t)(rem % 60);
    const uint8_t tlv[] = {0x09,       CMD_SET_DATETIME,
                           0x07,       (uint8_t)((y >> 8) & 0xFF),
                           (uint8_t)y, (uint8_t)mo,
                           (uint8_t)d, hh,
                           mi,         ss};
    Serial.printf("[gopro] set clock %04d-%02u-%02u %02u:%02u:%02u\n", y, mo, d, hh, mi, ss);
    return writeCmd(tlv, sizeof(tlv));
}

void GoProCamera::requestStatuses() {
    const uint8_t tlv[] = {0x07,         QUERY_GET_STATUS_VAL, ST_BUSY,
                           ST_ENCODING,  ST_ENC_DURATION,      ST_REMAIN_VIDEO,
                           ST_SD_REMAIN, ST_BATTERY_PCT};
    writeQuery(tlv, sizeof(tlv));
}

void GoProCamera::requestSettings() {
    const uint8_t tlv[] = {0x03, QUERY_GET_SETTING_VAL, SET_RESOLUTION, SET_FPS};
    writeQuery(tlv, sizeof(tlv));
}

void GoProCamera::keepAlive() {
    const uint8_t tlv[] = {0x03, SETTING_KEEP_ALIVE, 0x01, 0x42};
    writeSetting(tlv, sizeof(tlv));
}

void GoProCamera::onNotify(const NimBLEUUID& uuid, const uint8_t* data, size_t len) {
    status_.lastUpdateMs = millis();
    if (uuid == gp("0077")) {
        const uint8_t* msg  = nullptr;
        size_t         mlen = 0;
        if (rxQuery_->feed(data, len, &msg, &mlen))
            handleQueryResponse(msg, mlen);
    }
}

void GoProCamera::handleQueryResponse(const uint8_t* tlv, size_t len) {
    // [query_id][status][ (id, len, value) ... ]. Status (0x13) and setting (0x12) responses
    // share the shape but their ID spaces overlap, branch on query_id
    if (len < 2 || tlv[1] != 0x00)
        return;
    const bool isSetting = (tlv[0] == QUERY_GET_SETTING_VAL);
    size_t     i         = 2;
    while (i + 2 <= len) {
        const uint8_t id = tlv[i];
        const uint8_t vl = tlv[i + 1];
        if (i + 2 + vl > len)
            break;
        const uint8_t* v = tlv + i + 2;
        if (isSetting) {
            switch (id) {
                case SET_RESOLUTION:
                    if (vl >= 1) {
                        strncpy(status_.resolution, goproResolution(v[0]),
                                sizeof(status_.resolution) - 1);
                        status_.resolution[sizeof(status_.resolution) - 1] = '\0';
                    }
                    break;
                case SET_FPS:
                    if (vl >= 1)
                        status_.fps = goproFps(v[0]);
                    break;
                default:
                    break;
            }
        } else {
            switch (id) {
                case ST_ENCODING:
                    status_.recState = (vl >= 1 && v[0]) ? RecState::Recording : RecState::Idle;
                    break;
                case ST_ENC_DURATION:
                    if (vl >= 1)
                        status_.recElapsedS = beU(v, vl);
                    break;
                case ST_REMAIN_VIDEO:
                    if (vl >= 1)
                        status_.recLeftS = beU(v, vl);
                    break;
                case ST_SD_REMAIN:
                    if (vl >= 1)
                        status_.sdFreeMB = beU(v, vl) / 1024;
                    break;
                case ST_BATTERY_PCT:
                    if (vl >= 1)
                        status_.batteryPct = v[0];
                    break;
                default:
                    break;
            }
        }
        i += 2 + vl;
    }
}
