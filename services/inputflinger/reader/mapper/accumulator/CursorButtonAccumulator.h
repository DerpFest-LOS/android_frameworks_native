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

#pragma once

#include <stdint.h>

namespace android {

class InputDeviceContext;
struct RawEvent;

/* Keeps track of the state of mouse or touch pad buttons. */
class CursorButtonAccumulator {
public:
    CursorButtonAccumulator();
    void reset(const InputDeviceContext& deviceContext);

    void process(const RawEvent& rawEvent);

    uint32_t getButtonState() const;
    inline bool isLeftPressed() const { return mBtnLeft; }
    inline bool isRightPressed() const { return mBtnRight; }
    inline bool isMiddlePressed() const { return mBtnMiddle; }
    inline bool isBackPressed() const { return mBtnBack; }
    inline bool isSidePressed() const { return mBtnSide; }
    inline bool isForwardPressed() const { return mBtnForward; }
    inline bool isExtraPressed() const { return mBtnExtra; }
    inline bool isTaskPressed() const { return mBtnTask; }

    void setSwapLeftRightButtons(bool shouldSwap);

private:
    bool mBtnLeft;
    bool mBtnRight;
    bool mBtnMiddle;
    bool mBtnBack;
    bool mBtnSide;
    bool mBtnForward;
    bool mBtnExtra;
    bool mBtnTask;

    bool mSwapLeftRightButtons = false;

    void clearButtons();
};

} // namespace android
