/*
 * Copyright (C) 2019 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define LOG_TAG "InputDispatcher"

#include "Entry.h"

#include "Connection.h"
#include "DebugConfig.h"

#include <android-base/stringprintf.h>
#include <cutils/atomic.h>
#include <ftl/enum.h>
#include <inttypes.h>

using android::base::StringPrintf;

namespace android::inputdispatcher {

VerifiedKeyEvent verifiedKeyEventFromKeyEntry(const KeyEntry& entry) {
    return {{VerifiedInputEvent::Type::KEY, entry.deviceId, entry.eventTime, entry.source,
             entry.displayId},
            entry.action,
            entry.flags & VERIFIED_KEY_EVENT_FLAGS,
            entry.downTime,
            entry.keyCode,
            entry.scanCode,
            entry.metaState,
            entry.repeatCount};
}

VerifiedMotionEvent verifiedMotionEventFromMotionEntry(const MotionEntry& entry,
                                                       const ui::Transform& rawTransform) {
    const vec2 rawXY = MotionEvent::calculateTransformedXY(entry.source, rawTransform,
                                                           entry.pointerCoords[0].getXYValue());
    const int actionMasked = entry.action & AMOTION_EVENT_ACTION_MASK;
    return {{VerifiedInputEvent::Type::MOTION, entry.deviceId, entry.eventTime, entry.source,
             entry.displayId},
            rawXY.x,
            rawXY.y,
            actionMasked,
            entry.flags & VERIFIED_MOTION_EVENT_FLAGS,
            entry.downTime,
            entry.metaState,
            entry.buttonState};
}

// --- EventEntry ---

EventEntry::EventEntry(int32_t id, Type type, nsecs_t eventTime, uint32_t policyFlags)
      : id(id),
        type(type),
        eventTime(eventTime),
        policyFlags(policyFlags),
        injectionState(nullptr),
        dispatchInProgress(false) {}

// --- DeviceResetEntry ---

DeviceResetEntry::DeviceResetEntry(int32_t id, nsecs_t eventTime, int32_t deviceId)
      : EventEntry(id, Type::DEVICE_RESET, eventTime, 0), deviceId(deviceId) {}

std::string DeviceResetEntry::getDescription() const {
    return StringPrintf("DeviceResetEvent(deviceId=%d), policyFlags=0x%08x", deviceId, policyFlags);
}

// --- FocusEntry ---

// Focus notifications always go to apps, so set the flag POLICY_FLAG_PASS_TO_USER for all entries
FocusEntry::FocusEntry(int32_t id, nsecs_t eventTime, sp<IBinder> connectionToken, bool hasFocus,
                       const std::string& reason)
      : EventEntry(id, Type::FOCUS, eventTime, POLICY_FLAG_PASS_TO_USER),
        connectionToken(connectionToken),
        hasFocus(hasFocus),
        reason(reason) {}

std::string FocusEntry::getDescription() const {
    return StringPrintf("FocusEvent(hasFocus=%s)", hasFocus ? "true" : "false");
}

// --- PointerCaptureChangedEntry ---

// PointerCaptureChanged notifications always go to apps, so set the flag POLICY_FLAG_PASS_TO_USER
// for all entries.
PointerCaptureChangedEntry::PointerCaptureChangedEntry(int32_t id, nsecs_t eventTime,
                                                       const PointerCaptureRequest& request)
      : EventEntry(id, Type::POINTER_CAPTURE_CHANGED, eventTime, POLICY_FLAG_PASS_TO_USER),
        pointerCaptureRequest(request) {}

std::string PointerCaptureChangedEntry::getDescription() const {
    return StringPrintf("PointerCaptureChangedEvent(pointerCaptureEnabled=%s)",
                        pointerCaptureRequest.isEnable() ? "true" : "false");
}

// --- DragEntry ---

// Drag notifications always go to apps, so set the flag POLICY_FLAG_PASS_TO_USER for all entries
DragEntry::DragEntry(int32_t id, nsecs_t eventTime, sp<IBinder> connectionToken, bool isExiting,
                     float x, float y)
      : EventEntry(id, Type::DRAG, eventTime, POLICY_FLAG_PASS_TO_USER),
        connectionToken(connectionToken),
        isExiting(isExiting),
        x(x),
        y(y) {}

std::string DragEntry::getDescription() const {
    return StringPrintf("DragEntry(isExiting=%s, x=%f, y=%f)", isExiting ? "true" : "false", x, y);
}

// --- KeyEntry ---

KeyEntry::KeyEntry(int32_t id, std::shared_ptr<InjectionState> injectionState, nsecs_t eventTime,
                   int32_t deviceId, uint32_t source, ui::LogicalDisplayId displayId,
                   uint32_t policyFlags, int32_t action, int32_t flags, int32_t keyCode,
                   int32_t scanCode, int32_t metaState, int32_t repeatCount, nsecs_t downTime)
      : EventEntry(id, Type::KEY, eventTime, policyFlags),
        deviceId(deviceId),
        source(source),
        displayId(displayId),
        action(action),
        keyCode(keyCode),
        scanCode(scanCode),
        metaState(metaState),
        downTime(downTime),
        syntheticRepeat(false),
        interceptKeyResult(KeyEntry::InterceptKeyResult::UNKNOWN),
        interceptKeyWakeupTime(0),
        flags(flags),
        repeatCount(repeatCount) {
    EventEntry::injectionState = std::move(injectionState);
}

std::string KeyEntry::getDescription() const {
    if (!IS_DEBUGGABLE_BUILD) {
        return "KeyEvent";
    }
    return StringPrintf("KeyEvent(deviceId=%d, eventTime=%" PRIu64 ", source=%s, displayId=%s, "
                        "action=%s, "
                        "flags=0x%08x, keyCode=%s(%d), scanCode=%d, metaState=0x%08x, "
                        "repeatCount=%d), policyFlags=0x%08x",
                        deviceId, eventTime, inputEventSourceToString(source).c_str(),
                        displayId.toString().c_str(), KeyEvent::actionToString(action), flags,
                        KeyEvent::getLabel(keyCode), keyCode, scanCode, metaState, repeatCount,
                        policyFlags);
}

std::ostream& operator<<(std::ostream& out, const KeyEntry& keyEntry) {
    out << keyEntry.getDescription();
    return out;
}

// --- TouchModeEntry ---

TouchModeEntry::TouchModeEntry(int32_t id, nsecs_t eventTime, bool inTouchMode,
                               ui::LogicalDisplayId displayId)
      : EventEntry(id, Type::TOUCH_MODE_CHANGED, eventTime, POLICY_FLAG_PASS_TO_USER),
        inTouchMode(inTouchMode),
        displayId(displayId) {}

std::string TouchModeEntry::getDescription() const {
    return StringPrintf("TouchModeEvent(inTouchMode=%s)", inTouchMode ? "true" : "false");
}

// --- MotionEntry ---

MotionEntry::MotionEntry(int32_t id, std::shared_ptr<InjectionState> injectionState,
                         nsecs_t eventTime, int32_t deviceId, uint32_t source,
                         ui::LogicalDisplayId displayId, uint32_t policyFlags, int32_t action,
                         int32_t actionButton, int32_t flags, int32_t metaState,
                         int32_t buttonState, MotionClassification classification,
                         int32_t edgeFlags, float xPrecision, float yPrecision,
                         float xCursorPosition, float yCursorPosition, nsecs_t downTime,
                         const std::vector<PointerProperties>& pointerProperties,
                         const std::vector<PointerCoords>& pointerCoords)
      : EventEntry(id, Type::MOTION, eventTime, policyFlags),
        deviceId(deviceId),
        source(source),
        displayId(displayId),
        action(action),
        actionButton(actionButton),
        flags(flags),
        metaState(metaState),
        buttonState(buttonState),
        classification(classification),
        edgeFlags(edgeFlags),
        xPrecision(xPrecision),
        yPrecision(yPrecision),
        xCursorPosition(xCursorPosition),
        yCursorPosition(yCursorPosition),
        downTime(downTime),
        pointerProperties(pointerProperties),
        pointerCoords(pointerCoords) {
    EventEntry::injectionState = std::move(injectionState);
}

std::string MotionEntry::getDescription() const {
    if (!IS_DEBUGGABLE_BUILD) {
        return "MotionEvent";
    }
    std::string msg;
    msg += StringPrintf("MotionEvent(deviceId=%d, eventTime=%" PRIu64
                        ", source=%s, displayId=%s, action=%s, actionButton=0x%08x, flags=0x%08x,"
                        " metaState=0x%08x, "
                        "buttonState=0x%08x, "
                        "classification=%s, edgeFlags=0x%08x, xPrecision=%.1f, yPrecision=%.1f, "
                        "xCursorPosition=%0.1f, yCursorPosition=%0.1f, pointers=[",
                        deviceId, eventTime, inputEventSourceToString(source).c_str(),
                        displayId.toString().c_str(), MotionEvent::actionToString(action).c_str(),
                        actionButton, flags, metaState, buttonState,
                        motionClassificationToString(classification), edgeFlags, xPrecision,
                        yPrecision, xCursorPosition, yCursorPosition);

    for (uint32_t i = 0; i < getPointerCount(); i++) {
        if (i) {
            msg += ", ";
        }
        msg += StringPrintf("%d: (%.1f, %.1f)", pointerProperties[i].id, pointerCoords[i].getX(),
                            pointerCoords[i].getY());
    }
    msg += StringPrintf("]), policyFlags=0x%08x", policyFlags);
    return msg;
}

std::ostream& operator<<(std::ostream& out, const MotionEntry& motionEntry) {
    out << motionEntry.getDescription();
    return out;
}

// --- SensorEntry ---

SensorEntry::SensorEntry(int32_t id, nsecs_t eventTime, int32_t deviceId, uint32_t source,
                         uint32_t policyFlags, nsecs_t hwTimestamp,
                         InputDeviceSensorType sensorType, InputDeviceSensorAccuracy accuracy,
                         bool accuracyChanged, std::vector<float> values)
      : EventEntry(id, Type::SENSOR, eventTime, policyFlags),
        deviceId(deviceId),
        source(source),
        sensorType(sensorType),
        accuracy(accuracy),
        accuracyChanged(accuracyChanged),
        hwTimestamp(hwTimestamp),
        values(std::move(values)) {}

std::string SensorEntry::getDescription() const {
    std::string msg;
    msg += StringPrintf("SensorEntry(deviceId=%d, source=%s, sensorType=%s, "
                        "accuracy=%s, hwTimestamp=%" PRId64,
                        deviceId, inputEventSourceToString(source).c_str(),
                        ftl::enum_string(sensorType).c_str(), ftl::enum_string(accuracy).c_str(),
                        hwTimestamp);

    if (IS_DEBUGGABLE_BUILD) {
        for (size_t i = 0; i < values.size(); i++) {
            if (i > 0) {
                msg += ", ";
            }
            msg += StringPrintf("(%.3f)", values[i]);
        }
    }
    msg += StringPrintf(", policyFlags=0x%08x", policyFlags);
    return msg;
}

// --- DispatchEntry ---

volatile int32_t DispatchEntry::sNextSeqAtomic;

DispatchEntry::DispatchEntry(std::shared_ptr<const EventEntry> eventEntry,
                             ftl::Flags<InputTargetFlags> targetFlags,
                             const ui::Transform& transform, const ui::Transform& rawTransform,
                             float globalScaleFactor, gui::Uid targetUid, int64_t vsyncId,
                             std::optional<int32_t> windowId)
      : seq(nextSeq()),
        eventEntry(std::move(eventEntry)),
        targetFlags(targetFlags),
        transform(transform),
        rawTransform(rawTransform),
        globalScaleFactor(globalScaleFactor),
        deliveryTime(0),
        resolvedFlags(0),
        targetUid(targetUid),
        vsyncId(vsyncId),
        windowId(windowId) {
    switch (this->eventEntry->type) {
        case EventEntry::Type::KEY: {
            const KeyEntry& keyEntry = static_cast<const KeyEntry&>(*this->eventEntry);
            resolvedFlags = keyEntry.flags;
            break;
        }
        case EventEntry::Type::MOTION: {
            const MotionEntry& motionEntry = static_cast<const MotionEntry&>(*this->eventEntry);
            resolvedFlags = motionEntry.flags;
            break;
        }
        default: {
            break;
        }
    }
}

uint32_t DispatchEntry::nextSeq() {
    // Sequence number 0 is reserved and will never be returned.
    uint32_t seq;
    do {
        seq = android_atomic_inc(&sNextSeqAtomic);
    } while (!seq);
    return seq;
}

std::ostream& operator<<(std::ostream& out, const DispatchEntry& entry) {
    std::string transform;
    entry.transform.dump(transform, "transform");
    out << "DispatchEntry{resolvedFlags=" << entry.resolvedFlags
        << ", targetFlags=" << entry.targetFlags.string() << ", transform=" << transform
        << "} original: " << entry.eventEntry->getDescription();
    return out;
}

} // namespace android::inputdispatcher
