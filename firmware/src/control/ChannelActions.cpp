#include "control/ChannelActions.h"

#include <Arduino.h>

void ChannelActions::loadFromModes(const ModeRange* modes, ShutterVideoMode svm,
                                   ShutterVideoMode modeStyle, uint16_t startDelayMs,
                                   uint16_t stopDelayMs, OpMode opMode) {
    for (int i = 0; i < FUNC_COUNT; i++)
        modes_[i] = modes[i];
    svm_          = svm;
    modeStyle_    = modeStyle;
    startDelayMs_ = startDelayMs;
    stopDelayMs_  = stopDelayMs;
    opMode_       = opMode;
    pendingRec_   = REC_NONE;  // cancel any queued record command on reconfigure
    primed_       = false;
    armPrimed_    = false;
}

void ChannelActions::scheduleRec(Camera& cam, bool start) {
    const uint16_t delay = start ? startDelayMs_ : stopDelayMs_;
    if (delay == 0) {
        start ? cam.startRecord() : cam.stopRecord();
        return;
    }
    pendingRec_  = start ? REC_START : REC_STOP;
    pendingAtMs_ = millis() + delay;
}

void ChannelActions::update(const RcState& rc, Camera& cam, bool armed) {
    const uint32_t now = millis();

    if (pendingRec_ != REC_NONE && (int32_t)(now - pendingAtMs_) >= 0) {
        (pendingRec_ == REC_START) ? cam.startRecord() : cam.stopRecord();
        pendingRec_ = REC_NONE;
    }

    // Record-on-arm
    if (opMode_ == OpMode::RecordOnArm) {
        if (!armPrimed_) {
            prevArmed_ = armed;
            armPrimed_ = true;
        } else if (armed != prevArmed_) {
            prevArmed_ = armed;
            scheduleRec(cam, armed);
        }
        return;
    }

    if (!rc.valid)
        return;

    bool active[FUNC_COUNT];
    for (int i = 0; i < FUNC_COUNT; i++) {
        const ModeRange& m = modes_[i];
        if (m.aux == 0) {
            active[i] = false;
        } else {
            const uint16_t v = rc.aux(m.aux);
            active[i]        = (v >= m.rangeMin && v <= m.rangeMax);
        }
    }

    if (!primed_) {
        for (int i = 0; i < FUNC_COUNT; i++)
            prevActive_[i] = active[i];
        primed_ = true;
        return;
    }

    for (int i = 0; i < FUNC_COUNT; i++) {
        if (active[i] == prevActive_[i])
            continue;
        const Func f      = (Func)i;
        const bool rising = active[i];
        prevActive_[i]    = active[i];

        if (f == Func::Shutter) {
            const bool photo = cam.status().mode == CamMode::Photo;
            if (photo) {
                if (rising)
                    cam.takePhoto();
            } else if (svm_ == ShutterVideoMode::TwoPos) {
                scheduleRec(cam, rising);
            } else if (rising) {
                scheduleRec(cam, !cam.status().isRecording());
            }
            continue;
        }

        if (f == Func::CameraMode) {
            if (modeStyle_ == ShutterVideoMode::TwoPos) {
                cam.setMode(rising ? CamMode::Photo : CamMode::Video);
            } else if (rising) {
                cam.setMode(cam.status().mode == CamMode::Photo ? CamMode::Video : CamMode::Photo);
            }
            continue;
        }

        if (rising) {
            switch (f) {
                case Func::PresetVideo:
                    cam.loadPresetGroup(PresetGroup::Video);
                    break;
                case Func::PresetPhoto:
                    cam.loadPresetGroup(PresetGroup::Photo);
                    break;
                case Func::PresetTimelapse:
                    cam.loadPresetGroup(PresetGroup::Timelapse);
                    break;
                default:
                    break;
            }
        }
    }
}
