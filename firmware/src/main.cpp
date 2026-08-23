#include <Arduino.h>

#include "StatusLed.h"
#include "camera/BleTxPower.h"
#include "camera/dji/DjiActionCamera.h"
#include "camera/dji/DjiOsmoCamera.h"
#include "camera/gopro/GoProCamera.h"
#include "config.h"
#include "config/Settings.h"
#include "control/ChannelActions.h"
#include "fc/BetaflightMsp.h"
#include "net/WebConfig.h"
#include "osd/OsdRenderer.h"

static BetaflightMsp  fc(FC_UART, FC_BAUD, FC_RX_PIN, FC_TX_PIN, FC_RC_POLL_MS);
static ChannelActions actions;
static OsdRenderer    osd(fc, g_settings, OSD_UPDATE_MS, OSD_REFRESH_MS);
static WebConfig      web(g_settings);
static StatusLed      led(STATUS_LED_PIN);
static Camera*        cam = nullptr;

enum CamType : uint8_t { CAM_DJI_OSMO = 0, CAM_DJI_ACTION = 1, CAM_GOPRO = 2 };

static Camera* makeCamera(const Settings& s) {
    switch (s.camType) {
        case CAM_DJI_ACTION:
            return new DjiActionCamera(s.camMac);
        case CAM_GOPRO:
            return new GoProCamera(s.camMac, "GoPro");
        case CAM_DJI_OSMO:
        default:
            return new DjiOsmoCamera(s.camMac, s.camNamePrefix);
    }
}

static uint32_t lastStatus = 0;

static void applyActions() {
    actions.loadFromModes(g_settings.modes, (ShutterVideoMode)g_settings.shutterVideoMode,
                          (ShutterVideoMode)g_settings.modeSwitchStyle,
                          g_settings.recordStartDelayMs, g_settings.recordStopDelayMs,
                          (OpMode)g_settings.opMode);
}

// The camera's BLE stack uses blocking calls (connect, service discovery)
static void cameraTask(void*) {
    cam->begin();
    for (;;) {
        cam->poll();
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

void setup() {
    Serial.begin(CONSOLE_BAUD);
    delay(300);
    Serial.println("\nShutterBridge starting");

    g_settings.load();
    Serial.printf("[cfg] v=%u opMode=%u disarmedOnly=%u ssid=\"%s\"\n", g_settings.version,
                  g_settings.opMode, g_settings.webuiDisarmedOnly, g_settings.apSsid);
    cam = makeCamera(g_settings);
    // Start the BLE camera on its own core-0 task before the rest so NimBLE is up early
    xTaskCreatePinnedToCore(cameraTask, "camera", 8192, nullptr, 1, nullptr, 0);
    fc.begin();
    applyActions();
    web.attachStatus(&cam->status(), &fc.rc());
    web.attachCamera(cam);
    web.begin();
    led.begin();
}

void loop() {
    fc.poll();

    const RcState& rc    = fc.rc();
    const uint32_t now   = millis();
    const bool     armed = fc.armedKnown() && fc.armed();  // from the FC over MSP

    actions.update(rc, *cam, armed);

    const CameraStatus s = cam->status();  // snapshot (written by the camera task)
    const bool online = cam->isConnected() && s.lastUpdateMs != 0 && (now - s.lastUpdateMs < 2500);
    osd.update(s, online);

    web.poll(armed);
    if (web.takeDirty()) {
        applyActions();
        applyBleTxPower();
    }

    // Sync the GoPro clock from the FC's GPS-derived UTC, once per connection
    static bool clockSynced = false;
    if (!cam->isConnected()) {
        clockSynced = false;
    } else if (!clockSynced && g_settings.clockSync && g_settings.camType == CAM_GOPRO &&
               fc.time().valid) {
        const FcTime&     t = fc.time();
        Camera::WallClock wc{t.year, t.month, t.day, t.hours, t.minutes, t.seconds};
        if (cam->setDateTime(wc, g_settings.tzOffsetMin))
            clockSynced = true;
    }

    led.update(online, s.isRecording(), web.clientConnected());

    if (now - lastStatus >= 1000) {
        lastStatus = now;
        Serial.printf("cam:%s %s %02lu:%02lu batt %u%%  |  AUX1=%u armed:%u rc:%s\n",
                      online ? "on" : "off", toString(s.recState),
                      (unsigned long)(s.recElapsedS / 60), (unsigned long)(s.recElapsedS % 60),
                      s.batteryPct, rc.aux(1), armed, rc.valid ? "ok" : "--");
    }
}
