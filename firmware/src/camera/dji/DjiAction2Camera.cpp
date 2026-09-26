#include "camera/dji/DjiAction2Camera.h"

#include <Arduino.h>

#include <cstring>

#include "camera/BleTxPower.h"
#include "net/RadioCoex.h"

namespace {
    constexpr uint16_t UUID_SERVICE = 0xFFF0;
    constexpr uint16_t UUID_WRITE   = 0xFFF5;
    constexpr uint16_t UUID_NOTIFY  = 0xFFF4;

    constexpr uint8_t ADDR_WIFI = 0x07;  // pairing is handled by the camera's Wi-Fi module

    // The camera files its approval under this identifier - keep it constant so a reconnect
    // takes the "already paired" path instead of asking again. The token is only displayed
    const char PAIR_ID[]    = "001749319286102";
    const char PAIR_TOKEN[] = "osmo";

    constexpr uint32_t kPairTimeoutMs = 30000;  // time to tap approve on the camera
    constexpr uint32_t kKeepAliveMs   = 5000;
}  // namespace

class DjiAction2Camera::ClientCB : public NimBLEClientCallbacks {
   public:
    explicit ClientCB(DjiAction2Camera* owner) : owner_(owner) {
    }
    void onDisconnect(NimBLEClient*, int reason) override {
        owner_->connected_     = false;
        owner_->approved_      = false;
        owner_->chWrite_       = nullptr;
        owner_->chNotify_      = nullptr;
        owner_->status_        = CameraStatus{};
        owner_->status_.paired = false;
        Serial.printf("[a2] disconnected (reason 0x%02x)\n", reason);
    }

   private:
    DjiAction2Camera* owner_;
};

DjiAction2Camera::DjiAction2Camera(const char* macAddress) : mac_(macAddress ? macAddress : "") {
}

bool DjiAction2Camera::begin() {
    NimBLEDevice::init("ShutterBridge");
    applyBleTxPower();
    cb_ = new ClientCB(this);
    return attempt();
}

bool DjiAction2Camera::attempt() {
    if (mac_.empty())
        return false;
    if (!haveAddr_) {
        addr_     = NimBLEAddress(mac_, BLE_ADDR_PUBLIC);
        haveAddr_ = true;
        Serial.printf("[a2] bound MAC %s\n", addr_.toString().c_str());
    }
    return connect();
}

bool DjiAction2Camera::connect() {
    Serial.printf("[a2] connecting to %s ...\n", addr_.toString().c_str());
    if (!client_)
        client_ = NimBLEDevice::createClient();
    client_->setClientCallbacks(cb_, /*deleteCallbacks=*/false);
    client_->setConnectTimeout(g_apHasClient ? 2000 : 5000);

    if (!client_->connect(addr_)) {
        const int rc = client_->getLastError();
        Serial.printf("[a2] connect failed (rc=%d %s)\n", rc, NimBLEUtils::returnCodeToString(rc));
        return false;
    }

    NimBLERemoteService* svc = client_->getService(NimBLEUUID(UUID_SERVICE));
    if (!svc) {
        Serial.println("[a2] service fff0 not found");
        client_->disconnect();
        return false;
    }
    chNotify_ = svc->getCharacteristic(NimBLEUUID(UUID_NOTIFY));
    chWrite_  = svc->getCharacteristic(NimBLEUUID(UUID_WRITE));
    if (!chNotify_ || !chWrite_) {
        Serial.println("[a2] fff4/fff5 not found");
        client_->disconnect();
        return false;
    }

    scanner_.reset();
    status_        = CameraStatus{};
    status_.paired = false;
    status_.mode   = CamMode::Video;  // no mode readout - the shutter always drives recording
    approved_      = false;

    auto onData = [this](NimBLERemoteCharacteristic*, uint8_t* d, size_t n, bool) {
        onNotify(d, n);
    };
    chNotify_->subscribe(true, onData);
    if (chWrite_->canNotify())
        chWrite_->subscribe(true, onData);

    // Arm pairing: a plain write of 01 00 to the fff4 value itself (not its CCCD)
    if (chNotify_->canWrite()) {
        const uint8_t arm[] = {0x01, 0x00};
        chNotify_->writeValue(arm, sizeof(arm), /*response=*/true);
    }

    connected_        = true;
    status_.connected = true;
    connectedAtMs_    = millis();
    lastKeepMs_       = connectedAtMs_;
    delay(200);
    sendPairing();
    return true;
}

void DjiAction2Camera::sendPairing() {
    // SetPairingPIN (0x07/0x45): length-prefixed identifier, then length-prefixed token
    constexpr size_t idLen  = sizeof(PAIR_ID) - 1;
    constexpr size_t tokLen = sizeof(PAIR_TOKEN) - 1;
    uint8_t          pl[2 + idLen + tokLen];
    pl[0] = idLen;
    memcpy(pl + 1, PAIR_ID, idLen);
    pl[1 + idLen] = tokLen;
    memcpy(pl + 2 + idLen, PAIR_TOKEN, tokLen);
    Serial.println("[a2] pairing request sent");
    writeCmd(ADDR_WIFI, 0x07, 0x45, pl, sizeof(pl));
}

void DjiAction2Camera::keepAlive() {
    const uint8_t p = 0x00;
    writeCmd(duml::ADDR_CAM, 0x00, 0xF1, &p, 1);
}

void DjiAction2Camera::setApproved(const char* how) {
    approved_      = true;
    status_.paired = true;
    Serial.printf("[a2] session open (%s)\n", how);
}

void DjiAction2Camera::drop(const char* why) {
    Serial.printf("[a2] link dead (%s) - dropping\n", why);
    if (client_)
        client_->disconnect();
    connected_     = false;
    approved_      = false;
    status_        = CameraStatus{};
    lastAttemptMs_ = millis();  // let the disconnect finish before the next connect attempt
}

void DjiAction2Camera::poll() {
    if (connected_) {
        const uint32_t t = millis();
        if (!approved_) {
            if (t - connectedAtMs_ > kPairTimeoutMs)
                drop("not approved on the camera");
        } else {
            // Once paired the camera goes quiet - no status pushes, no reply to the keep-alive -
            // so the BLE link itself (supervision timeout -> onDisconnect) is the liveness
            // signal, and the record time runs from our own accepted start
            status_.lastUpdateMs = t;
            if (status_.isRecording())
                status_.recElapsedS = (t - recStartMs_) / 1000;
            if (t - lastKeepMs_ >= kKeepAliveMs) {
                lastKeepMs_ = t;
                keepAlive();
            }
        }
    }

    // Reconnect at exponential backoff, throttled while connected to the Web UI
    const uint32_t now        = millis();
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

bool DjiAction2Camera::writeRaw(const uint8_t* data, size_t len) {
    if (!connected_ || !chWrite_)
        return false;
    return chWrite_->writeValue(data, len, /*response=*/false);
}

bool DjiAction2Camera::writeCmd(uint8_t receiver, uint8_t cmdSet, uint8_t cmdId,
                                const uint8_t* payload, size_t payloadLen) {
    auto f = duml::buildFrame(duml::ADDR_APP, receiver, cmdSet, cmdId, payload, payloadLen,
                              duml::FLAG_CMD, seq_++);
    return writeRaw(f.data(), f.size());
}

bool DjiAction2Camera::recordCtrl(bool start) {
    if (!approved_) {
        Serial.println("[a2] record ignored - not paired (approve on the camera first)");
        return false;
    }
    const uint8_t p = start ? 0x01 : 0x00;
    lastCmdStart_   = start;
    if (!writeCmd(duml::ADDR_CAM, 0x02, 0x02, &p, 1))
        return false;
    // Optimistic until the camera's reply (handleFrame) confirms or rejects it
    if (start && !status_.isRecording()) {
        recStartMs_         = millis();
        status_.recElapsedS = 0;
    }
    status_.recState = start ? RecState::Recording : RecState::Idle;
    return true;
}

bool DjiAction2Camera::startRecord() {
    return recordCtrl(true);
}
bool DjiAction2Camera::stopRecord() {
    return recordCtrl(false);
}
bool DjiAction2Camera::takePhoto() {
    Serial.println("[a2] photo capture not supported");
    return false;
}

void DjiAction2Camera::onNotify(const uint8_t* data, size_t len) {
    scanner_.feed(data, len, [this](const duml::Frame& f) { handleFrame(f); });
}

void DjiAction2Camera::handleFrame(const duml::Frame& f) {
    const uint8_t* p = f.payload;
    const size_t   n = f.payloadLen;

    // Pairing status reply: 01 = already paired, 02 = waiting for approval on the camera
    if (f.cmdSet == 0x07 && f.cmdId == 0x45 && f.flags == duml::FLAG_ACK && n >= 2) {
        if (p[1] == 0x01)
            setApproved("already paired");
        else if (p[1] == 0x02)
            Serial.println("[a2] approve the connection on the camera screen");
        else
            Serial.printf("[a2] pairing status %02x %02x\n", p[0], p[1]);
        return;
    }
    // Approved on the camera screen - acknowledge it
    if (f.cmdSet == 0x07 && f.cmdId == 0x46 && f.flags == duml::FLAG_CMD) {
        const uint8_t ok  = 0x00;
        auto          ack = duml::buildFrame(f.receiver, f.sender, 0x07, 0x46, &ok, 1,
                                             duml::FLAG_ACK, f.seq);
        writeRaw(ack.data(), ack.size());
        setApproved("approved on the camera");
        return;
    }
    // Record reply: 00 = OK, d8 busy, d9 wrong state, e0 not supported
    if (f.cmdSet == 0x02 && f.cmdId == 0x02 && f.flags == duml::FLAG_ACK && n >= 1) {
        if (p[0] == 0x00)
            status_.recState = lastCmdStart_ ? RecState::Recording : RecState::Idle;
        else
            Serial.printf("[a2] record command rejected (0x%02x)\n", p[0]);
        return;
    }
    // Battery push, same layout as on the other DJI cameras (unverified on the Action 2)
    if (f.cmdSet == 0x0D && f.cmdId == 0x02 && n >= 21) {
        status_.batteryPct = p[20];
        return;
    }
}
