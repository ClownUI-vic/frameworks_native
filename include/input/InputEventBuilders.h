/*
 * Copyright 2023 The Android Open Source Project
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

#pragma once

#include <android/input.h>
#include <attestation/HmacKeyManager.h>
#include <input/Input.h>
#include <input/InputTransport.h>
#include <ui/LogicalDisplayId.h>
#include <utils/Timers.h> // for nsecs_t, systemTime

#include <vector>

namespace android {

// An arbitrary device id.
static constexpr uint32_t DEFAULT_DEVICE_ID = 1;

// The default policy flags to use for event injection by tests.
static constexpr uint32_t DEFAULT_POLICY_FLAGS = POLICY_FLAG_FILTERED | POLICY_FLAG_PASS_TO_USER;

class PointerBuilder {
public:
    PointerBuilder(int32_t id, ToolType toolType) {
        mProperties.clear();
        mProperties.id = id;
        mProperties.toolType = toolType;
        mCoords.clear();
    }

    PointerBuilder& x(float x) { return axis(AMOTION_EVENT_AXIS_X, x); }

    PointerBuilder& y(float y) { return axis(AMOTION_EVENT_AXIS_Y, y); }

    PointerBuilder& isResampled(bool isResampled) {
        mCoords.isResampled = isResampled;
        return *this;
    }

    PointerBuilder& axis(int32_t axis, float value) {
        mCoords.setAxisValue(axis, value);
        return *this;
    }

    PointerProperties buildProperties() const { return mProperties; }

    PointerCoords buildCoords() const { return mCoords; }

private:
    PointerProperties mProperties;
    PointerCoords mCoords;
};

class InputMessageBuilder {
public:
    InputMessageBuilder(InputMessage::Type type, uint32_t seq) : mType{type}, mSeq{seq} {}

    InputMessageBuilder& eventId(int32_t eventId) {
        mEventId = eventId;
        return *this;
    }

    InputMessageBuilder& eventTime(nsecs_t eventTime) {
        mEventTime = eventTime;
        return *this;
    }

    InputMessageBuilder& deviceId(DeviceId deviceId) {
        mDeviceId = deviceId;
        return *this;
    }

    InputMessageBuilder& source(int32_t source) {
        mSource = source;
        return *this;
    }

    InputMessageBuilder& displayId(ui::LogicalDisplayId displayId) {
        mDisplayId = displayId;
        return *this;
    }

    InputMessageBuilder& action(int32_t action) {
        mAction = action;
        return *this;
    }

    InputMessageBuilder& downTime(nsecs_t downTime) {
        mDownTime = downTime;
        return *this;
    }

    InputMessageBuilder& pointer(PointerBuilder pointerBuilder) {
        mPointers.push_back(pointerBuilder);
        return *this;
    }

    InputMessage build() const {
        InputMessage message{};
        // Header
        message.header.type = mType;
        message.header.seq = mSeq;
        // Body
        message.body.motion.eventId = mEventId;
        message.body.motion.pointerCount = mPointers.size();
        message.body.motion.eventTime = mEventTime;
        message.body.motion.deviceId = mDeviceId;
        message.body.motion.source = mSource;
        message.body.motion.displayId = mDisplayId.val();
        message.body.motion.action = mAction;
        message.body.motion.downTime = mDownTime;

        for (size_t i = 0; i < mPointers.size(); ++i) {
            message.body.motion.pointers[i].properties = mPointers[i].buildProperties();
            message.body.motion.pointers[i].coords = mPointers[i].buildCoords();
        }
        return message;
    }

private:
    const InputMessage::Type mType;
    const uint32_t mSeq;

    int32_t mEventId{InputEvent::nextId()};
    nsecs_t mEventTime{systemTime(SYSTEM_TIME_MONOTONIC)};
    DeviceId mDeviceId{DEFAULT_DEVICE_ID};
    int32_t mSource{AINPUT_SOURCE_TOUCHSCREEN};
    ui::LogicalDisplayId mDisplayId{ui::LogicalDisplayId::DEFAULT};
    int32_t mAction{AMOTION_EVENT_ACTION_MOVE};
    nsecs_t mDownTime{mEventTime};

    std::vector<PointerBuilder> mPointers;
};

class MotionEventBuilder {
public:
    MotionEventBuilder(int32_t action, int32_t source) {
        mAction = action;
        mSource = source;
        mEventTime = systemTime(SYSTEM_TIME_MONOTONIC);
        mDownTime = mEventTime;
    }

    MotionEventBuilder& deviceId(int32_t deviceId) {
        mDeviceId = deviceId;
        return *this;
    }

    MotionEventBuilder& downTime(nsecs_t downTime) {
        mDownTime = downTime;
        return *this;
    }

    MotionEventBuilder& eventTime(nsecs_t eventTime) {
        mEventTime = eventTime;
        return *this;
    }

    MotionEventBuilder& displayId(ui::LogicalDisplayId displayId) {
        mDisplayId = displayId;
        return *this;
    }

    MotionEventBuilder& actionButton(int32_t actionButton) {
        mActionButton = actionButton;
        return *this;
    }

    MotionEventBuilder& buttonState(int32_t buttonState) {
        mButtonState = buttonState;
        return *this;
    }

    MotionEventBuilder& rawXCursorPosition(float rawXCursorPosition) {
        mRawXCursorPosition = rawXCursorPosition;
        return *this;
    }

    MotionEventBuilder& rawYCursorPosition(float rawYCursorPosition) {
        mRawYCursorPosition = rawYCursorPosition;
        return *this;
    }

    MotionEventBuilder& pointer(PointerBuilder pointer) {
        mPointers.push_back(pointer);
        return *this;
    }

    MotionEventBuilder& addFlag(uint32_t flags) {
        mFlags |= flags;
        return *this;
    }

    MotionEventBuilder& transform(ui::Transform t) {
        mTransform = t;
        return *this;
    }

    MotionEventBuilder& rawTransform(ui::Transform t) {
        mRawTransform = t;
        return *this;
    }

    MotionEvent build() const {
        std::vector<PointerProperties> pointerProperties;
        std::vector<PointerCoords> pointerCoords;
        for (const PointerBuilder& pointer : mPointers) {
            pointerProperties.push_back(pointer.buildProperties());
            pointerCoords.push_back(pointer.buildCoords());
        }

        auto [xCursorPosition, yCursorPosition] =
                std::make_pair(mRawXCursorPosition, mRawYCursorPosition);
        // Set mouse cursor position for the most common cases to avoid boilerplate.
        if (mSource == AINPUT_SOURCE_MOUSE &&
            !MotionEvent::isValidCursorPosition(xCursorPosition, yCursorPosition)) {
            xCursorPosition = pointerCoords[0].getX();
            yCursorPosition = pointerCoords[0].getY();
        }

        MotionEvent event;
        event.initialize(InputEvent::nextId(), mDeviceId, mSource, mDisplayId, INVALID_HMAC,
                         mAction, mActionButton, mFlags, /*edgeFlags=*/0, AMETA_NONE, mButtonState,
                         MotionClassification::NONE, mTransform,
                         /*xPrecision=*/0, /*yPrecision=*/0, xCursorPosition, yCursorPosition,
                         mRawTransform, mDownTime, mEventTime, mPointers.size(),
                         pointerProperties.data(), pointerCoords.data());
        return event;
    }

private:
    int32_t mAction;
    int32_t mDeviceId{DEFAULT_DEVICE_ID};
    int32_t mSource;
    nsecs_t mDownTime;
    nsecs_t mEventTime;
    ui::LogicalDisplayId mDisplayId{ui::LogicalDisplayId::DEFAULT};
    int32_t mActionButton{0};
    int32_t mButtonState{0};
    int32_t mFlags{0};
    float mRawXCursorPosition{AMOTION_EVENT_INVALID_CURSOR_POSITION};
    float mRawYCursorPosition{AMOTION_EVENT_INVALID_CURSOR_POSITION};
    ui::Transform mTransform;
    ui::Transform mRawTransform;

    std::vector<PointerBuilder> mPointers;
};

class KeyEventBuilder {
public:
    KeyEventBuilder(int32_t action, int32_t source) {
        mAction = action;
        mSource = source;
        mEventTime = systemTime(SYSTEM_TIME_MONOTONIC);
        mDownTime = mEventTime;
    }

    KeyEventBuilder(const KeyEvent& event) {
        mAction = event.getAction();
        mDeviceId = event.getDeviceId();
        mSource = event.getSource();
        mDownTime = event.getDownTime();
        mEventTime = event.getEventTime();
        mDisplayId = event.getDisplayId();
        mFlags = event.getFlags();
        mKeyCode = event.getKeyCode();
        mScanCode = event.getScanCode();
        mMetaState = event.getMetaState();
        mRepeatCount = event.getRepeatCount();
    }

    KeyEventBuilder& deviceId(int32_t deviceId) {
        mDeviceId = deviceId;
        return *this;
    }

    KeyEventBuilder& downTime(nsecs_t downTime) {
        mDownTime = downTime;
        return *this;
    }

    KeyEventBuilder& eventTime(nsecs_t eventTime) {
        mEventTime = eventTime;
        return *this;
    }

    KeyEventBuilder& displayId(ui::LogicalDisplayId displayId) {
        mDisplayId = displayId;
        return *this;
    }

    KeyEventBuilder& policyFlags(int32_t policyFlags) {
        mPolicyFlags = policyFlags;
        return *this;
    }

    KeyEventBuilder& addFlag(uint32_t flags) {
        mFlags |= flags;
        return *this;
    }

    KeyEventBuilder& keyCode(int32_t keyCode) {
        mKeyCode = keyCode;
        return *this;
    }

    KeyEventBuilder& repeatCount(int32_t repeatCount) {
        mRepeatCount = repeatCount;
        return *this;
    }

    KeyEvent build() const {
        KeyEvent event{};
        event.initialize(InputEvent::nextId(), mDeviceId, mSource, mDisplayId, INVALID_HMAC,
                         mAction, mFlags, mKeyCode, mScanCode, mMetaState, mRepeatCount, mDownTime,
                         mEventTime);
        return event;
    }

private:
    int32_t mAction;
    int32_t mDeviceId = DEFAULT_DEVICE_ID;
    uint32_t mSource;
    nsecs_t mDownTime;
    nsecs_t mEventTime;
    ui::LogicalDisplayId mDisplayId{ui::LogicalDisplayId::DEFAULT};
    uint32_t mPolicyFlags = DEFAULT_POLICY_FLAGS;
    int32_t mFlags{0};
    int32_t mKeyCode{AKEYCODE_UNKNOWN};
    int32_t mScanCode{0};
    int32_t mMetaState{AMETA_NONE};
    int32_t mRepeatCount{0};
};

} // namespace android
