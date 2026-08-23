#include "osd/OsdRenderer.h"

#include <Arduino.h>
#include <stdio.h>
#include <string.h>

void OsdRenderer::formatField(OsdField f, const CameraStatus& st, bool online, char* out,
                              size_t n) {
    out[0] = '\0';
    if (!online) {
        if (f == OsdField::RecStatus)
            snprintf(out, n, "CAM OFF");
        return;
    }
    switch (f) {
        case OsdField::RecStatus:
            if (!st.ready)
                snprintf(out, n, "CAM NOT READY");  // not in a photo/video shooting mode
            else if (s_.opMode == (uint8_t)OpMode::RecordOnArm && st.mode == CamMode::Photo)
                snprintf(out, n, "PHOTO NO-REC!");  // record-on-arm can't record in photo mode
            else if (st.isRecording())
                snprintf(out, n, "REC %u:%02u", (unsigned)(st.recElapsedS / 60),
                         (unsigned)(st.recElapsedS % 60));
            else
                snprintf(out, n, "NOT REC");
            break;
        case OsdField::Battery:
            snprintf(out, n, "CAM %u%%", (unsigned)st.batteryPct);
            break;
        case OsdField::Mode:
            snprintf(out, n, "%s", st.mode == CamMode::Photo ? "PHOTO" : "VIDEO");
            break;
        case OsdField::SdFree:
            snprintf(out, n, "%uG", (unsigned)((st.sdFreeMB + 512) / 1024));
            break;
        case OsdField::TimeLeft:
            snprintf(out, n, "%u:%02u", (unsigned)(st.recLeftS / 60), (unsigned)(st.recLeftS % 60));
            break;
        case OsdField::Resolution:
            // Blank (cleared slot) when unreported - e.g. Osmo Nano doesn't expose it.
            snprintf(out, n, "%s", st.resolution);
            break;
        case OsdField::Fps:
            if (st.fps)
                snprintf(out, n, "%uFPS", (unsigned)st.fps);
            break;
        case OsdField::Off:
        default:
            break;
    }
}

void OsdRenderer::update(const CameraStatus& st, bool online) {
    const uint32_t now = millis();
    if (now - lastMs_ < updateMs_)
        return;
    lastMs_ = now;

    // The FC/goggle OSD retains the last strings we drew, and we normally only re-send a slot
    // when its text changes. If an early write is lost (e.g. FC still booting), a stale frame
    // would linger forever. Periodically re-send every slot so the display always converges.
    bool forceAll = false;
    if (now - lastRefreshMs_ >= refreshMs_) {
        lastRefreshMs_ = now;
        forceAll       = true;
    }

    for (uint8_t slot = 0; slot < 4; slot++) {
        char buf[32];
        formatField(s_.osdSlot[slot], st, online, buf, sizeof(buf));
        if (buf[0] == '\0') {
            buf[0] = ' ';
            buf[1] = '\0';
        }
        if (forceAll || strncmp(buf, last_[slot], sizeof(buf)) != 0) {
            strncpy(last_[slot], buf, sizeof(last_[slot]));
            fc_.setOsdMessage(slot, buf);  // empty string clears the element
        }
    }
}
