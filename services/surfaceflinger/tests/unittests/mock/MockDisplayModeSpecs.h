/*
 * Copyright 2022 The Android Open Source Project
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

#include <android/gui/DisplayModeSpecs.h>

#include "DisplayHardware/DisplayMode.h"

namespace android::mock {

inline gui::DisplayModeSpecs createDisplayModeSpecs(DisplayModeId defaultMode, Fps maxFps,
                                                    bool allowGroupSwitching = false) {
    gui::DisplayModeSpecs specs;
    specs.defaultMode = ftl::to_underlying(defaultMode);
    specs.allowGroupSwitching = allowGroupSwitching;
    specs.primaryRanges.physical.min = 0.f;
    specs.primaryRanges.physical.max = maxFps.getValue();
    specs.primaryRanges.render = specs.primaryRanges.physical;
    specs.appRequestRanges = specs.primaryRanges;
    return specs;
}

} // namespace android::mock
