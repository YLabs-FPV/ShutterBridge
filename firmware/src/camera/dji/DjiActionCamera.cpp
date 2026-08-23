#include "camera/dji/DjiActionCamera.h"

#include <Arduino.h>
#include <esp_random.h>

#include <cstring>

#include "camera/BleTxPower.h"
#include "net/RadioCoex.h"

namespace {
    constexpr uint16_t UUID_SERVICE = 0xFFF0;
    constexpr uint16_t UUID_WRITE   = 0xFFF5;
    constexpr uint16_t UUID_NOTIFY  = 0xFFF4;

    constexpr uint32_t CONN_DEVICE_ID = 0x12345678;
    constexpr uint32_t REC_DEVICE_ID  = 0x33FF0000;
    const uint8_t      CTRL_MAC[6]    = {0x38, 0x34, 0x56, 0x78, 0x9A, 0xBC};

    inline void put_le16(uint8_t* p, uint16_t v) {
        p[0] = v & 0xFF;
        p[1] = (v >> 8) & 0xFF;
    }
    inline void put_le32(uint8_t* p, uint32_t v) {
        p[0] = v & 0xFF;
        p[1] = (v >> 8) & 0xFF;
        p[2] = (v >> 16) & 0xFF;
        p[3] = (v >> 24) & 0xFF;
    }
    inline uint16_t le16(const uint8_t* p) {
        return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
    }
    inline uint32_t le32(const uint8_t* p) {
        return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
               ((uint32_t)p[3] << 24);
    }

    // Resolution / fps are enum indices from the DJI SDK, not raw values
    const char* djiResolution(uint8_t idx) {
        switch (idx) {
            case 10:
                return "1080P";
            case 16:
                return "4K";
            case 45:
                return "2.7K";
            case 66:
                return "1080P 9:16";
            case 67:
                return "2.7K 9:16";
            case 95:
                return "2.7K 4:3";
            case 103:
                return "4K 4:3";
            case 109:
                return "4K 9:16";
            default:
                return "";
        }
    }
    uint16_t djiFps(uint8_t idx) {
        switch (idx) {
            case 1:
                return 24;
            case 2:
                return 25;
            case 3:
                return 30;
            case 4:
                return 48;
            case 5:
                return 50;
            case 6:
                return 60;
            case 7:
                return 120;
            case 8:
                return 240;
            case 10:
                return 100;
            case 19:
                return 200;
            default:
                return 0;
        }
    }
}  // namespace

class DjiActionCamera::ClientCB : public NimBLEClientCallbacks {
   public:
    explicit ClientCB(DjiActionCamera* owner) : owner_(owner) {
    }
    void onDisconnect(NimBLEClient*, int reason) override {
        owner_->connected_     = false;
        owner_->chWrite_       = nullptr;
        owner_->chNotify_      = nullptr;
        owner_->proto_         = PROTO_NONE;
        owner_->status_        = CameraStatus{};
        owner_->status_.paired = false;
        Serial.printf("[act] disconnected (reason 0x%02x)\n", reason);
    }

   private:
    DjiActionCamera* owner_;
};

DjiActionCamera::DjiActionCamera(const char* macAddress) : mac_(macAddress ? macAddress : "") {
}

bool DjiActionCamera::begin() {
    NimBLEDevice::init("ShutterBridge");
    applyBleTxPower();
    cb_ = new ClientCB(this);
    return attempt();
}

bool DjiActionCamera::attempt() {
    if (mac_.empty())
        return false;
    if (!haveAddr_) {
        addr_     = NimBLEAddress(mac_, BLE_ADDR_PUBLIC);
        haveAddr_ = true;
        Serial.printf("[act] bound MAC %s\n", addr_.toString().c_str());
    }
    return connect();
}

bool DjiActionCamera::connect() {
    Serial.printf("[act] connecting to %s ...\n", addr_.toString().c_str());
    if (!client_)
        client_ = NimBLEDevice::createClient();
    client_->setClientCallbacks(cb_, /*deleteCallbacks=*/false);
    client_->setConnectTimeout(g_apHasClient ? 2000 : 5000);

    if (!client_->connect(addr_)) {
        Serial.println("[act] connect failed");
        return false;
    }

    NimBLERemoteService* svc = client_->getService(NimBLEUUID(UUID_SERVICE));
    if (!svc) {
        Serial.println("[act] service fff0 not found");
        client_->disconnect();
        return false;
    }
    chNotify_ = svc->getCharacteristic(NimBLEUUID(UUID_NOTIFY));
    chWrite_  = svc->getCharacteristic(NimBLEUUID(UUID_WRITE));
    if (!chNotify_ || !chWrite_) {
        Serial.println("[act] fff4/fff5 not found");
        client_->disconnect();
        return false;
    }

    rscan_.reset();
    dscan_.reset();
    status_        = CameraStatus{};
    status_.paired = false;
    chNotify_->subscribe(
        true, [this](NimBLERemoteCharacteristic*, uint8_t* d, size_t n, bool) { onNotify(d, n); });

    connected_        = true;
    status_.connected = true;
    connectedAtMs_    = millis();
    proto_            = PROTO_CONNECTING;
    sendConnectionRequest();
    return true;
}

void DjiActionCamera::sendConnectionRequest() {
    // verify_mode 0 makes the camera show a pairing PIN on first connect; accept it on-screen
    // and the camera replies verify_data 0. The bond is then remembered
    verifyData_    = (uint16_t)(esp_random() % 10000);
    uint8_t pl[33] = {0};
    put_le32(pl + 0, CONN_DEVICE_ID);
    pl[4] = sizeof(CTRL_MAC);
    memcpy(pl + 5, CTRL_MAC, sizeof(CTRL_MAC));
    put_le16(pl + 27, verifyData_);
    Serial.println("[act] connection request - accept the PIN on the camera screen");
    sendR(0x00, 0x19, djir::CMD_WAIT_RESULT, pl, sizeof(pl), seq_++);
}

void DjiActionCamera::subscribeStatus() {
    uint8_t pl[6] = {2, 20, 0, 0, 0, 0};  // periodic push at 2 Hz
    sendR(0x1D, 0x05, djir::CMD_NO_RESPONSE, pl, sizeof(pl), seq_++);
}

void DjiActionCamera::poll() {
    const uint32_t now = millis();

    if (connected_) {
        const bool stale = status_.lastUpdateMs != 0 && (now - status_.lastUpdateMs) > 5000;
        // Long grace before first telemetry: the camera is silent until the PIN is accepted
        const bool noTelemetry = status_.lastUpdateMs == 0 && (now - connectedAtMs_) > 60000;
        if (stale || noTelemetry) {
            Serial.printf("[act] link dead (%s) - dropping\n", stale ? "stale" : "no telemetry");
            if (client_)
                client_->disconnect();
            connected_ = false;
            proto_     = PROTO_NONE;
            status_    = CameraStatus{};
        }
    }

    // Reconnect at exponential backoff, throttled while connected to the Web UI
    const uint32_t kApRetryMs = 8000;
    const uint32_t interval = (g_apHasClient && backoffMs_ < kApRetryMs) ? kApRetryMs : backoffMs_;
    if (!connected_ && (now - lastAttemptMs_ > interval)) {
        lastAttemptMs_ = now;
        if (attempt()) {
            backoffMs_ = kBackoffMinMs;
        } else {
            backoffMs_ = backoffMs_ < kBackoffMaxMs ? backoffMs_ * 2 : kBackoffMaxMs;
            if (backoffMs_ > kBackoffMaxMs)
                backoffMs_ = kBackoffMaxMs;
        }
    }
}

bool DjiActionCamera::writeRaw(const uint8_t* data, size_t len) {
    if (!connected_ || !chWrite_)
        return false;
    return chWrite_->writeValue(data, len, /*response=*/false);
}

bool DjiActionCamera::sendR(uint8_t cmdSet, uint8_t cmdId, uint8_t cmdType, const uint8_t* pl,
                            size_t n, uint16_t seq) {
    auto f = djir::buildFrame(cmdSet, cmdId, cmdType, pl, n, seq);
    return writeRaw(f.data(), f.size());
}

bool DjiActionCamera::recordCtrl(uint8_t ctrl) {
    if (proto_ != PROTO_CONNECTED) {
        Serial.println("[act] record ignored - not protocol-connected (accept the PIN first)");
        return false;
    }
    uint8_t pl[9] = {0};
    put_le32(pl + 0, REC_DEVICE_ID);
    pl[4] = ctrl;  // 0 = start, 1 = stop
    return sendR(0x1D, 0x03, djir::CMD_RESPONSE_OR_NOT, pl, sizeof(pl), seq_++);
}

bool DjiActionCamera::startRecord() {
    return recordCtrl(0x00);
}
bool DjiActionCamera::stopRecord() {
    return recordCtrl(0x01);
}
bool DjiActionCamera::takePhoto() {
    return recordCtrl(0x00);
}

bool DjiActionCamera::setMode(CamMode mode) {
    if (proto_ != PROTO_CONNECTED) {
        Serial.println("[act] setMode ignored - not protocol-connected");
        return false;
    }
    // camera_mode_t: Video 0x01, Photo 0x05
    const uint8_t mv    = (mode == CamMode::Photo) ? 0x05 : 0x01;
    uint8_t       pl[9] = {0x00, 0x00, 0x33, 0xFF, mv, 0x01, 0x47, 0x39, 0x36};
    Serial.printf("[act] setMode -> %s (0x%02x)\n", mode == CamMode::Photo ? "PHOTO" : "VIDEO", mv);
    return sendR(0x1D, 0x04, djir::CMD_RESPONSE_OR_NOT, pl, sizeof(pl), seq_++);
}

void DjiActionCamera::onNotify(const uint8_t* data, size_t len) {
    // The camera interleaves R-SDK (0xAA) and DUML (0x55) frames; each scanner keeps its own
    rscan_.feed(data, len, [this](const djir::Frame& f) { handleR(f); });
    dscan_.feed(data, len, [this](const duml::Frame& f) { handleDuml(f); });
}

void DjiActionCamera::handleR(const djir::Frame& f) {
    if (f.cmdSet == 0x00 && f.cmdId == 0x19) {  // connection handshake
        if (!f.isResponse() && f.payloadLen >= 29) {
            const uint8_t  vmode = f.payload[26];
            const uint16_t vdata = le16(f.payload + 27);
            if (vmode == 2 && vdata == 0) {  // camera approved
                uint8_t rp[9] = {0};
                put_le32(rp + 0, CONN_DEVICE_ID);
                sendR(0x00, 0x19, djir::ACK_NO_RESPONSE, rp, sizeof(rp), f.seq);
                proto_         = PROTO_CONNECTED;
                status_.paired = true;
                subscribeStatus();
                Serial.println("[act] protocol connected - subscribing status");
            } else {
                Serial.printf("[act] camera rejected connection (vmode=%u vdata=%u)\n", vmode,
                              vdata);
            }
        } else if (f.isResponse() && f.payloadLen >= 5) {
            Serial.printf("[act] connection ack ret_code=%u\n", f.payload[4]);
        }
        return;
    }

    if (f.cmdSet == 0x1D && f.cmdId == 0x02 && f.payloadLen >= 27) {  // status push
        const uint8_t* p         = f.payload;
        const uint8_t  camMode   = p[0];
        const uint8_t  camStatus = p[1];
        status_.recState         = (camStatus == 0x03) ? RecState::Recording : RecState::Idle;
        status_.mode             = (camMode == 0x05) ? CamMode::Photo : CamMode::Video;
        strncpy(status_.resolution, djiResolution(p[2]), sizeof(status_.resolution) - 1);
        status_.resolution[sizeof(status_.resolution) - 1] = '\0';
        status_.fps                                        = djiFps(p[3]);
        status_.recElapsedS                                = le16(p + 5);
        status_.sdFreeMB                                   = le32(p + 15);
        status_.recLeftS                                   = le32(p + 23);
        if (f.payloadLen >= 38)  // battery rides at the tail of the same push
            status_.batteryPct = p[37];
        status_.lastUpdateMs = millis();
        return;
    }

    if (f.cmdSet == 0x1D && f.cmdId == 0x03 && f.isResponse() && f.payloadLen >= 1) {
        Serial.printf("[act] record response ret_code=%u\n", f.payload[0]);
        return;
    }

    if (f.cmdSet == 0x1D && f.cmdId == 0x04 && f.isResponse() && f.payloadLen >= 1) {
        Serial.printf("[act] mode-switch response ret_code=%u\n", f.payload[0]);
        return;
    }
}

void DjiActionCamera::handleDuml(const duml::Frame& f) {
    if (f.cmdSet == 0x0D && f.cmdId == 0x02 && f.payloadLen >= 21) {  // battery
        status_.batteryMv  = le16(f.payload + 1);
        status_.batteryPct = f.payload[20];
    }
}
