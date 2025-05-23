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

#include <vector>

#include <ui/DisplayId.h>

#include "Display/DisplayModeRequest.h"

namespace android::scheduler {

struct ISchedulerCallback {
    virtual void requestHardwareVsync(PhysicalDisplayId, bool enabled) = 0;
    virtual void requestDisplayModes(std::vector<display::DisplayModeRequest>) = 0;
    virtual void kernelTimerChanged(bool expired) = 0;
    virtual void onChoreographerAttached() = 0;
    virtual void onExpectedPresentTimePosted(TimePoint, ftl::NonNull<DisplayModePtr>,
                                             Fps renderRate) = 0;
    virtual void onCommitNotComposited() = 0;
    virtual void vrrDisplayIdle(bool idle) = 0;

protected:
    ~ISchedulerCallback() = default;
};

} // namespace android::scheduler
