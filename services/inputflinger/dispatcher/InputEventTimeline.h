/*
 * Copyright (C) 2021 The Android Open Source Project
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

#include "../InputDeviceMetricsSource.h"

#include <binder/IBinder.h>
#include <input/Input.h>
#include <unordered_map>

namespace android {

namespace inputdispatcher {

/**
 * Describes the input event timeline for each connection.
 * An event with the same inputEventId can go to more than 1 connection simultaneously.
 * For each connection that the input event goes to, there will be a separate ConnectionTimeline
 * created.
 * To create a complete ConnectionTimeline, we must receive two calls:
 * 1) setDispatchTimeline
 * 2) setGraphicsTimeline
 *
 * In a typical scenario, the dispatch timeline is known first. Later, if a frame is produced, the
 * graphics timeline is available.
 */
struct ConnectionTimeline {
    // DispatchTimeline
    nsecs_t deliveryTime; // time at which the event was sent to the receiver
    nsecs_t consumeTime;  // time at which the receiver read the event
    nsecs_t finishTime;   // time at which the finish event was received
    // GraphicsTimeline
    std::array<nsecs_t, GraphicsTimeline::SIZE> graphicsTimeline;

    ConnectionTimeline(nsecs_t deliveryTime, nsecs_t consumeTime, nsecs_t finishTime);
    ConnectionTimeline(std::array<nsecs_t, GraphicsTimeline::SIZE> graphicsTimeline);

    /**
     * True if all contained timestamps are valid, false otherwise.
     */
    bool isComplete() const;
    /**
     * Set the dispatching-related times. Return true if the operation succeeded, false if the
     * dispatching times have already been set. If this function returns false, it likely indicates
     * an error from the app side.
     */
    bool setDispatchTimeline(nsecs_t deliveryTime, nsecs_t consumeTime, nsecs_t finishTime);
    /**
     * Set the graphics-related times. Return true if the operation succeeded, false if the
     * graphics times have already been set. If this function returns false, it likely indicates
     * an error from the app side.
     */
    bool setGraphicsTimeline(std::array<nsecs_t, GraphicsTimeline::SIZE> graphicsTimeline);

    inline bool operator==(const ConnectionTimeline& rhs) const;
    inline bool operator!=(const ConnectionTimeline& rhs) const;

private:
    bool mHasDispatchTimeline = false;
    bool mHasGraphicsTimeline = false;
};

enum class InputEventActionType : int32_t {
    UNKNOWN_INPUT_EVENT = 0,
    MOTION_ACTION_DOWN = 1,
    // Motion events for ACTION_MOVE (characterizes scrolling motion)
    MOTION_ACTION_MOVE = 2,
    // Motion events for ACTION_UP (when the pointer first goes up)
    MOTION_ACTION_UP = 3,
    // Motion events for ACTION_HOVER_MOVE (pointer position on screen changes but pointer is not
    // down)
    MOTION_ACTION_HOVER_MOVE = 4,
    // Motion events for ACTION_SCROLL (moving the mouse wheel)
    MOTION_ACTION_SCROLL = 5,
    // Key events for both ACTION_DOWN and ACTION_UP (key press and key release)
    KEY = 6,

    ftl_first = UNKNOWN_INPUT_EVENT,
    ftl_last = KEY,
    // Used by latency fuzzer
    kMaxValue = ftl_last

};

struct InputEventTimeline {
    InputEventTimeline(nsecs_t eventTime, nsecs_t readTime, uint16_t vendorId, uint16_t productId,
                       const std::set<InputDeviceUsageSource>& sources,
                       InputEventActionType inputEventActionType);
    const nsecs_t eventTime;
    const nsecs_t readTime;
    const uint16_t vendorId;
    const uint16_t productId;
    const std::set<InputDeviceUsageSource> sources;
    const InputEventActionType inputEventActionType;

    struct IBinderHash {
        std::size_t operator()(const sp<IBinder>& b) const {
            return std::hash<IBinder*>{}(b.get());
        }
    };

    std::unordered_map<sp<IBinder>, ConnectionTimeline, IBinderHash> connectionTimelines;

    bool operator==(const InputEventTimeline& rhs) const;
};

class InputEventTimelineProcessor {
protected:
    InputEventTimelineProcessor() {}

public:
    virtual ~InputEventTimelineProcessor() {}

    /**
     * Process the provided timeline
     */
    virtual void processTimeline(const InputEventTimeline& timeline) = 0;

    /**
     * Trigger a push for the input event latency statistics
     */
    virtual void pushLatencyStatistics() = 0;

    virtual std::string dump(const char* prefix) const = 0;
};

} // namespace inputdispatcher
} // namespace android
