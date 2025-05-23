/**
 * Copyright 2024 The Android Open Source Project
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

#include <functional>
#include <map>
#include <memory>
#include <optional>

#include <input/Input.h>
#include <input/InputTransport.h>
#include <input/Resampler.h>
#include <utils/Looper.h>

namespace android {

/**
 * An interface to receive batched input events. Even if you don't want batching, you still have to
 * use this interface, and some of the events will be batched if your implementation is slow to
 * handle the incoming input. The events received by these callbacks are never null.
 */
class InputConsumerCallbacks {
public:
    virtual ~InputConsumerCallbacks(){};
    virtual void onKeyEvent(std::unique_ptr<KeyEvent> event, uint32_t seq) = 0;
    virtual void onMotionEvent(std::unique_ptr<MotionEvent> event, uint32_t seq) = 0;
    /**
     * When you receive this callback, you must (eventually) call "consumeBatchedInputEvents".
     * If you don't want batching, then call "consumeBatchedInputEvents" immediately with
     * std::nullopt requestedFrameTime to receive the pending motion event(s).
     * @param pendingBatchSource the source of the pending batch.
     */
    virtual void onBatchedInputEventPending(int32_t pendingBatchSource) = 0;
    virtual void onFocusEvent(std::unique_ptr<FocusEvent> event, uint32_t seq) = 0;
    virtual void onCaptureEvent(std::unique_ptr<CaptureEvent> event, uint32_t seq) = 0;
    virtual void onDragEvent(std::unique_ptr<DragEvent> event, uint32_t seq) = 0;
    virtual void onTouchModeEvent(std::unique_ptr<TouchModeEvent> event, uint32_t seq) = 0;
};

/**
 * Consumes input events from an input channel.
 *
 * This is a re-implementation of InputConsumer. At the moment it only supports resampling for
 * single pointer events. A lot of the higher-level logic has been folded into this class, to make
 * it easier to use. In the legacy class, InputConsumer, the consumption logic was partially handled
 * in the jni layer, as well as various actions like adding the fd to the Choreographer.
 *
 * TODO(b/297226446): use this instead of "InputConsumer":
 * - Add resampling for multiple pointer events.
 * - Allow various resampling strategies to be specified
 * - Delete the old "InputConsumer" and use this class instead, renaming it to "InputConsumer".
 * - Add tracing
 * - Update all tests to use the new InputConsumer
 *
 * This class is not thread-safe. We are currently allowing the constructor to run on any thread,
 * but all of the remaining APIs should be invoked on the looper thread only.
 */
class InputConsumerNoResampling final {
public:
    /**
     * @param callbacks are used to interact with InputConsumerNoResampling. They're called whenever
     * the event is ready to consume.
     * @param looper needs to be sp and not shared_ptr because it inherits from
     * RefBase
     * @param resamplerCreator callable that returns the resampling strategy to be used. If null, no
     * resampling will be performed. resamplerCreator must never return nullptr.
     */
    explicit InputConsumerNoResampling(
            const std::shared_ptr<InputChannel>& channel, sp<Looper> looper,
            InputConsumerCallbacks& callbacks,
            std::function<std::unique_ptr<Resampler>()> resamplerCreator);

    ~InputConsumerNoResampling();

    /**
     * Must be called exactly once for each event received through the callbacks.
     */
    void finishInputEvent(uint32_t seq, bool handled);
    void reportTimeline(int32_t inputEventId, nsecs_t gpuCompletedTime, nsecs_t presentTime);
    /**
     * If you want to consume all events immediately (disable batching), then you still must call
     * this. For requestedFrameTime, use a std::nullopt. It is not guaranteed that the consumption
     * will occur at requestedFrameTime. The resampling strategy may modify it.
     * @param requestedFrameTime the time up to which consume the events. When there's double (or
     * triple) buffering, you may want to not consume all events currently available, because you
     * could be still working on an older frame, but there could already have been events that
     * arrived that are more recent.
     * @return whether any events were actually consumed
     */
    bool consumeBatchedInputEvents(std::optional<nsecs_t> requestedFrameTime);

    /**
     * Returns true when there is *likely* a pending batch or a pending event in the channel.
     *
     * This is only a performance hint and may return false negative results. Clients should not
     * rely on availability of the message based on the return value.
     */
    bool probablyHasInput() const;

    std::string getName() { return mChannel->getName(); }

    std::string dump() const;

private:
    std::shared_ptr<InputChannel> mChannel;
    sp<Looper> mLooper;
    InputConsumerCallbacks& mCallbacks;
    const std::function<std::unique_ptr<Resampler>()> mResamplerCreator;

    /**
     * A map to manage multidevice resampling. Each contained resampler is never null. This map is
     * only modified by handleMessages.
     */
    std::map<DeviceId, std::unique_ptr<Resampler>> mResamplers;

    // Looper-related infrastructure
    /**
     * This class is needed to associate the function "handleReceiveCallback" with the provided
     * looper. The callback sent to the looper is RefBase - based, so we can't just send a reference
     * of this class directly to the looper.
     */
    class LooperEventCallback : public LooperCallback {
    public:
        LooperEventCallback(std::function<int(int events)> callback) : mCallback(callback) {}
        int handleEvent(int /*fd*/, int events, void* /*data*/) override {
            return mCallback(events);
        }

    private:
        const std::function<int(int events)> mCallback;
    };
    sp<LooperEventCallback> mCallback;
    /**
     * The actual code that executes when the looper encounters available data on the InputChannel.
     */
    int handleReceiveCallback(int events);
    int mFdEvents;
    void setFdEvents(int events);

    void ensureCalledOnLooperThread(const char* func) const;

    // Event-reading infrastructure
    /**
     * A fifo queue of events to be sent to the InputChannel. We can't send all InputMessages to
     * the channel immediately when they are produced, because it's possible that the InputChannel
     * is blocked (if the channel buffer is full). When that happens, we don't want to drop the
     * events. Therefore, events should only be erased from the queue after they've been
     * successfully written to the InputChannel.
     */
    std::queue<InputMessage> mOutboundQueue;
    /**
     * Try to send all of the events in mOutboundQueue over the InputChannel. Not all events might
     * actually get sent, because it's possible that the channel is blocked.
     */
    void processOutboundEvents();

    /**
     * The time at which each event with the sequence number 'seq' was consumed.
     * This data is provided in 'finishInputEvent' so that the receiving end can measure the latency
     * This collection is populated when the event is received, and the entries are erased when the
     * events are finished. It should not grow infinitely because if an event is not ack'd, ANR
     * will be raised for that connection, and no further events will be posted to that channel.
     */
    std::unordered_map<uint32_t /*seq*/, nsecs_t /*consumeTime*/> mConsumeTimes;
    /**
     * Find and return the consumeTime associated with the provided sequence number. Crashes if
     * the provided seq number is not found.
     */
    nsecs_t popConsumeTime(uint32_t seq);

    // Event reading and processing
    /**
     * Read all of the available events from the InputChannel
     */
    std::vector<InputMessage> readAllMessages();

    /**
     * Send InputMessage to the corresponding InputConsumerCallbacks function.
     * @param msg
     */
    void handleMessage(const InputMessage& msg) const;

    // Batching
    /**
     * Batch messages that can be batched. When an unbatchable message is encountered, send it
     * to the InputConsumerCallbacks immediately. If there are batches remaining,
     * notify InputConsumerCallbacks. If a resampleable ACTION_DOWN message is received, then a
     * resampler is inserted for that deviceId in mResamplers. If a resampleable ACTION_UP or
     * ACTION_CANCEL message is received then the resampler associated to that deviceId is erased
     * from mResamplers.
     */
    void handleMessages(std::vector<InputMessage>&& messages);
    /**
     * Batched InputMessages, per deviceId.
     * For each device, we are storing a queue of batched messages. These will all be collapsed into
     * a single MotionEvent (up to a specific requestedFrameTime) when the consumer calls
     * `consumeBatchedInputEvents`.
     */
    std::map<DeviceId, std::queue<InputMessage>> mBatches;

    /**
     * Creates a MotionEvent by consuming samples from the provided queue. Consumes all messages
     * with eventTime <= requestedFrameTime - resampleLatency, where `resampleLatency` is latency
     * introduced by the resampler. Assumes that messages are queued in chronological order.
     * @param requestedFrameTime The time up to which consume messages, as given by the inequality
     * above. If std::nullopt, everything in messages will be consumed.
     * @param messages the queue of messages to consume from.
     */
    std::pair<std::unique_ptr<MotionEvent>, std::optional<uint32_t>> createBatchedMotionEvent(
            const std::optional<nsecs_t> requestedFrameTime, std::queue<InputMessage>& messages);

    /**
     * Consumes the batched input events, optionally allowing the caller to specify a device id
     * and/or requestedFrameTime threshold. It is not guaranteed that consumption will occur at
     * requestedFrameTime.
     * @param deviceId The device id from which to consume events. If std::nullopt, consumes events
     * from any device id.
     * @param requestedFrameTime The time up to which consume the events. If std::nullopt, consumes
     * input events with any timestamp.
     * @return Whether or not any events were consumed.
     */
    bool consumeBatchedInputEvents(std::optional<DeviceId> deviceId,
                                   std::optional<nsecs_t> requestedFrameTime);
    /**
     * A map from a single sequence number to several sequence numbers. This is needed because of
     * batching. When batching is enabled, a single MotionEvent will contain several samples. Each
     * sample came from an individual InputMessage of Type::Motion, and therefore will have to be
     * finished individually. Therefore, when the app calls "finish" on a (possibly batched)
     * MotionEvent, we will need to check this map in case there are multiple sequence numbers
     * associated with a single number that the app provided.
     *
     * For example:
     * Suppose we received 4 InputMessage's of type Motion, with action MOVE:
     * InputMessage(MOVE)   InputMessage(MOVE)   InputMessage(MOVE)   InputMessage(MOVE)
     *    seq=10               seq=11               seq=12               seq=13
     * The app consumed them all as a batch, which means that the app received a single MotionEvent
     * with historySize=3 and seq = 10.
     *
     * This map will look like:
     * {
     *   10: [11, 12, 13],
     * }
     * So the sequence number 10 will have 3 other sequence numbers associated with it.
     * When the app calls 'finish' for seq=10, we need to call 'finish' 4 times total, for sequence
     * numbers 10, 11, 12, 13. The app is not aware of the sequence numbers of each sample inside
     * the batched MotionEvent that it received.
     */
    std::map<uint32_t, std::vector<uint32_t>> mBatchedSequenceNumbers;
};

} // namespace android
