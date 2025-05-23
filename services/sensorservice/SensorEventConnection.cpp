/*
 * Copyright (C) 2010 The Android Open Source Project
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

#include <log/log.h>
#include <sys/socket.h>
#include <utils/threads.h>

#include <android/util/ProtoOutputStream.h>
#include <frameworks/base/core/proto/android/service/sensor_service.proto.h>
#include <sensor/SensorEventQueue.h>

#include "vec.h"
#include "BatteryService.h"
#include "SensorEventConnection.h"
#include "SensorDevice.h"

#define UNUSED(x) (void)(x)

namespace android {
namespace {

// Used as the default value for the target SDK until it's obtained via getTargetSdkVersion.
constexpr int kTargetSdkUnknown = 0;

}  // namespace

SensorService::SensorEventConnection::SensorEventConnection(
        const sp<SensorService>& service, uid_t uid, String8 packageName, bool isDataInjectionMode,
        const String16& opPackageName, const String16& attributionTag)
    : mService(service), mUid(uid), mWakeLockRefCount(0), mHasLooperCallbacks(false),
      mDead(false), mDataInjectionMode(isDataInjectionMode), mEventCache(nullptr),
      mCacheSize(0), mMaxCacheSize(0), mTimeOfLastEventDrop(0), mEventsDropped(0),
      mPackageName(packageName), mOpPackageName(opPackageName), mAttributionTag(attributionTag),
      mTargetSdk(kTargetSdkUnknown), mDestroyed(false) {
    mUserId = multiuser_get_user_id(mUid);
    mChannel = new BitTube(mService->mSocketBufferSize);
#if DEBUG_CONNECTIONS
    mEventsReceived = mEventsSentFromCache = mEventsSent = 0;
    mTotalAcksNeeded = mTotalAcksReceived = 0;
#endif
}

SensorService::SensorEventConnection::~SensorEventConnection() {
    ALOGD_IF(DEBUG_CONNECTIONS, "~SensorEventConnection(%p)", this);
    destroy();
    delete[] mEventCache;
}

void SensorService::SensorEventConnection::destroy() {
    if (!mDestroyed.exchange(true)) {
      mService->cleanupConnection(this);
    }
}

void SensorService::SensorEventConnection::onFirstRef() {
    LooperCallback::onFirstRef();
}

bool SensorService::SensorEventConnection::needsWakeLock() {
    Mutex::Autolock _l(mConnectionLock);
    return !mDead && mWakeLockRefCount > 0;
}

void SensorService::SensorEventConnection::resetWakeLockRefCount() {
    Mutex::Autolock _l(mConnectionLock);
    mWakeLockRefCount = 0;
}

void SensorService::SensorEventConnection::dump(String8& result) {
    Mutex::Autolock _l(mConnectionLock);
    result.appendFormat("\tOperating Mode: ");
    if (!mService->isAllowListedPackage(getPackageName())) {
        result.append("RESTRICTED\n");
    } else if (mDataInjectionMode) {
        result.append("DATA_INJECTION\n");
    } else {
        result.append("NORMAL\n");
    }
    result.appendFormat("\t %s | WakeLockRefCount %d | uid %d | cache size %d | "
                        "max cache size %d | has sensor access: %s\n",
                        mPackageName.c_str(), mWakeLockRefCount, mUid, mCacheSize, mMaxCacheSize,
                        hasSensorAccess() ? "true" : "false");
    for (auto& it : mSensorInfo) {
        const FlushInfo& flushInfo = it.second;
        result.appendFormat("\t %s 0x%08x | first flush pending: %s | pending flush events %d \n",
                            mService->getSensorName(it.first).c_str(), it.first,
                            flushInfo.mFirstFlushPending ? "true" : "false",
                            flushInfo.mPendingFlushEventsToSend);
    }
#if DEBUG_CONNECTIONS
    result.appendFormat("\t events recvd: %d | sent %d | cache %d | dropped %d |"
            " total_acks_needed %d | total_acks_recvd %d\n",
            mEventsReceived,
            mEventsSent,
            mEventsSentFromCache,
            mEventsReceived - (mEventsSentFromCache + mEventsSent + mCacheSize),
            mTotalAcksNeeded,
            mTotalAcksReceived);
#endif
}

/**
 * Dump debugging information as android.service.SensorEventConnectionProto protobuf message using
 * ProtoOutputStream.
 *
 * See proto definition and some notes about ProtoOutputStream in
 * frameworks/base/core/proto/android/service/sensor_service.proto
 */
void SensorService::SensorEventConnection::dump(util::ProtoOutputStream* proto) const {
    using namespace service::SensorEventConnectionProto;
    Mutex::Autolock _l(mConnectionLock);

    if (!mService->isAllowListedPackage(getPackageName())) {
        proto->write(OPERATING_MODE, OP_MODE_RESTRICTED);
    } else if (mDataInjectionMode) {
        proto->write(OPERATING_MODE, OP_MODE_DATA_INJECTION);
    } else {
        proto->write(OPERATING_MODE, OP_MODE_NORMAL);
    }
    proto->write(PACKAGE_NAME, std::string(mPackageName.c_str()));
    proto->write(WAKE_LOCK_REF_COUNT, int32_t(mWakeLockRefCount));
    proto->write(UID, int32_t(mUid));
    proto->write(CACHE_SIZE, int32_t(mCacheSize));
    proto->write(MAX_CACHE_SIZE, int32_t(mMaxCacheSize));
    for (auto& it : mSensorInfo) {
        const FlushInfo& flushInfo = it.second;
        const uint64_t token = proto->start(FLUSH_INFOS);
        proto->write(FlushInfoProto::SENSOR_NAME,
                std::string(mService->getSensorName(it.first)));
        proto->write(FlushInfoProto::SENSOR_HANDLE, it.first);
        proto->write(FlushInfoProto::FIRST_FLUSH_PENDING, flushInfo.mFirstFlushPending);
        proto->write(FlushInfoProto::PENDING_FLUSH_EVENTS_TO_SEND,
                flushInfo.mPendingFlushEventsToSend);
        proto->end(token);
    }
#if DEBUG_CONNECTIONS
    proto->write(EVENTS_RECEIVED, mEventsReceived);
    proto->write(EVENTS_SENT, mEventsSent);
    proto->write(EVENTS_CACHE, mEventsSentFromCache);
    proto->write(EVENTS_DROPPED, mEventsReceived - (mEventsSentFromCache + mEventsSent +
            mCacheSize));
    proto->write(TOTAL_ACKS_NEEDED, mTotalAcksNeeded);
    proto->write(TOTAL_ACKS_RECEIVED, mTotalAcksReceived);
#endif
}

bool SensorService::SensorEventConnection::addSensor(int32_t handle) {
    Mutex::Autolock _l(mConnectionLock);
    std::shared_ptr<SensorInterface> si = mService->getSensorInterfaceFromHandle(handle);
    if (si == nullptr ||
        !mService->canAccessSensor(si->getSensor(), "Add to SensorEventConnection: ",
                                   mOpPackageName) ||
        mSensorInfo.count(handle) > 0) {
        return false;
    }
    mSensorInfo[handle] = FlushInfo();
    return true;
}

bool SensorService::SensorEventConnection::removeSensor(int32_t handle) {
    Mutex::Autolock _l(mConnectionLock);
    if (mSensorInfo.erase(handle) > 0) {
        return true;
    }
    return false;
}

std::vector<int32_t> SensorService::SensorEventConnection::getActiveSensorHandles() const {
    Mutex::Autolock _l(mConnectionLock);
    std::vector<int32_t> list;
    for (auto& it : mSensorInfo) {
        list.push_back(it.first);
    }
    return list;
}

bool SensorService::SensorEventConnection::hasSensor(int32_t handle) const {
    Mutex::Autolock _l(mConnectionLock);
    return mSensorInfo.count(handle) > 0;
}

bool SensorService::SensorEventConnection::hasAnySensor() const {
    Mutex::Autolock _l(mConnectionLock);
    return mSensorInfo.size() ? true : false;
}

bool SensorService::SensorEventConnection::hasOneShotSensors() const {
    Mutex::Autolock _l(mConnectionLock);
    for (auto &it : mSensorInfo) {
        const int handle = it.first;
        std::shared_ptr<SensorInterface> si = mService->getSensorInterfaceFromHandle(handle);
        if (si != nullptr && si->getSensor().getReportingMode() == AREPORTING_MODE_ONE_SHOT) {
            return true;
        }
    }
    return false;
}

String8 SensorService::SensorEventConnection::getPackageName() const {
    return mPackageName;
}

void SensorService::SensorEventConnection::setFirstFlushPending(int32_t handle,
                                bool value) {
    Mutex::Autolock _l(mConnectionLock);
    if (mSensorInfo.count(handle) > 0) {
        FlushInfo& flushInfo = mSensorInfo[handle];
        flushInfo.mFirstFlushPending = value;
    }
}

void SensorService::SensorEventConnection::updateLooperRegistration(const sp<Looper>& looper) {
    Mutex::Autolock _l(mConnectionLock);
    updateLooperRegistrationLocked(looper);
}

void SensorService::SensorEventConnection::updateLooperRegistrationLocked(
        const sp<Looper>& looper) {
    bool isConnectionActive = (mSensorInfo.size() > 0 && !mDataInjectionMode) ||
                              mDataInjectionMode;
    // If all sensors are unregistered OR Looper has encountered an error, we can remove the Fd from
    // the Looper if it has been previously added.
    if (!isConnectionActive || mDead) { if (mHasLooperCallbacks) {
        ALOGD_IF(DEBUG_CONNECTIONS, "%p removeFd fd=%d", this,
                 mChannel->getSendFd());
        looper->removeFd(mChannel->getSendFd()); mHasLooperCallbacks = false; }
    return; }

    int looper_flags = 0;
    if (mCacheSize > 0) looper_flags |= ALOOPER_EVENT_OUTPUT;
    if (mDataInjectionMode) looper_flags |= ALOOPER_EVENT_INPUT;
    for (auto& it : mSensorInfo) {
        const int handle = it.first;
        std::shared_ptr<SensorInterface> si = mService->getSensorInterfaceFromHandle(handle);
        if (si != nullptr && si->getSensor().isWakeUpSensor()) {
            looper_flags |= ALOOPER_EVENT_INPUT;
        }
    }

    // If flags is still set to zero, we don't need to add this fd to the Looper, if the fd has
    // already been added, remove it. This is likely to happen when ALL the events stored in the
    // cache have been sent to the corresponding app.
    if (looper_flags == 0) {
        if (mHasLooperCallbacks) {
            ALOGD_IF(DEBUG_CONNECTIONS, "removeFd fd=%d", mChannel->getSendFd());
            looper->removeFd(mChannel->getSendFd());
            mHasLooperCallbacks = false;
        }
        return;
    }

    // Add the file descriptor to the Looper for receiving acknowledegments if the app has
    // registered for wake-up sensors OR for sending events in the cache.
    int ret = looper->addFd(mChannel->getSendFd(), 0, looper_flags, this, nullptr);
    if (ret == 1) {
        ALOGD_IF(DEBUG_CONNECTIONS, "%p addFd fd=%d", this, mChannel->getSendFd());
        mHasLooperCallbacks = true;
    } else {
        ALOGE("Looper::addFd failed ret=%d fd=%d", ret, mChannel->getSendFd());
    }
}

bool SensorService::SensorEventConnection::incrementPendingFlushCountIfHasAccess(int32_t handle) {
    if (hasSensorAccess()) {
        Mutex::Autolock _l(mConnectionLock);
        if (mSensorInfo.count(handle) > 0) {
            FlushInfo& flushInfo = mSensorInfo[handle];
            flushInfo.mPendingFlushEventsToSend++;
        }
        return true;
    } else {
        return false;
    }
}

status_t SensorService::SensorEventConnection::sendEvents(
        sensors_event_t const* buffer, size_t numEvents,
        sensors_event_t* scratch,
        wp<const SensorEventConnection> const * mapFlushEventsToConnections) {
    // filter out events not for this connection

    std::unique_ptr<sensors_event_t[]> sanitizedBuffer;

    int count = 0;
    Mutex::Autolock _l(mConnectionLock);
    if (scratch) {
        size_t i=0;
        while (i<numEvents) {
            int32_t sensor_handle = buffer[i].sensor;
            if (buffer[i].type == SENSOR_TYPE_META_DATA) {
                ALOGD_IF(DEBUG_CONNECTIONS, "flush complete event sensor==%d ",
                        buffer[i].meta_data.sensor);
                // Setting sensor_handle to the correct sensor to ensure the sensor events per
                // connection are filtered correctly.  buffer[i].sensor is zero for meta_data
                // events.
                sensor_handle = buffer[i].meta_data.sensor;
            }

            // Check if this connection has registered for this sensor. If not continue to the
            // next sensor_event.
            if (mSensorInfo.count(sensor_handle) == 0) {
                ++i;
                continue;
            }

            FlushInfo& flushInfo = mSensorInfo[sensor_handle];
            // Check if there is a pending flush_complete event for this sensor on this connection.
            if (buffer[i].type == SENSOR_TYPE_META_DATA && flushInfo.mFirstFlushPending == true &&
                    mapFlushEventsToConnections[i] == this) {
                flushInfo.mFirstFlushPending = false;
                ALOGD_IF(DEBUG_CONNECTIONS, "First flush event for sensor==%d ",
                        buffer[i].meta_data.sensor);
                ++i;
                continue;
            }

            // If there is a pending flush complete event for this sensor on this connection,
            // ignore the event and proceed to the next.
            if (flushInfo.mFirstFlushPending) {
                ++i;
                continue;
            }

            do {
                // Keep copying events into the scratch buffer as long as they are regular
                // sensor_events are from the same sensor_handle OR they are flush_complete_events
                // from the same sensor_handle AND the current connection is mapped to the
                // corresponding flush_complete_event.
                if (buffer[i].type == SENSOR_TYPE_META_DATA) {
                    if (mapFlushEventsToConnections[i] == this) {
                        scratch[count++] = buffer[i];
                    }
                } else {
                    // Regular sensor event, just copy it to the scratch buffer after checking
                    // the AppOp.
                    if (hasSensorAccess() && noteOpIfRequired(buffer[i])) {
                        scratch[count++] = buffer[i];
                    }
                }
                i++;
            } while ((i<numEvents) && ((buffer[i].sensor == sensor_handle &&
                                        buffer[i].type != SENSOR_TYPE_META_DATA) ||
                                       (buffer[i].type == SENSOR_TYPE_META_DATA  &&
                                        buffer[i].meta_data.sensor == sensor_handle)));
        }
    } else {
        if (hasSensorAccess()) {
            scratch = const_cast<sensors_event_t *>(buffer);
            count = numEvents;
        } else {
            sanitizedBuffer.reset(new sensors_event_t[numEvents]);
            scratch = sanitizedBuffer.get();
            for (size_t i = 0; i < numEvents; i++) {
                if (buffer[i].type == SENSOR_TYPE_META_DATA) {
                    scratch[count++] = buffer[i++];
                }
            }
        }
    }

    sendPendingFlushEventsLocked();
    // Early return if there are no events for this connection.
    if (count == 0) {
        return status_t(NO_ERROR);
    }

#if DEBUG_CONNECTIONS
     mEventsReceived += count;
#endif
    if (mCacheSize != 0) {
        // There are some events in the cache which need to be sent first. Copy this buffer to
        // the end of cache.
        appendEventsToCacheLocked(scratch, count);
        return status_t(NO_ERROR);
    }

    int index_wake_up_event = -1;
    if (hasSensorAccess()) {
        index_wake_up_event = findWakeUpSensorEventLocked(scratch, count);
        if (index_wake_up_event >= 0) {
            BatteryService::noteWakeupSensorEvent(scratch[index_wake_up_event].timestamp,
                                                  mUid, scratch[index_wake_up_event].sensor);
            scratch[index_wake_up_event].flags |= WAKE_UP_SENSOR_EVENT_NEEDS_ACK;
            ++mWakeLockRefCount;
#if DEBUG_CONNECTIONS
            ++mTotalAcksNeeded;
#endif
        }
    }

    // NOTE: ASensorEvent and sensors_event_t are the same type.
    ssize_t size = SensorEventQueue::write(mChannel,
                                    reinterpret_cast<ASensorEvent const*>(scratch), count);
    if (size < 0) {
        // Write error, copy events to local cache.
        if (index_wake_up_event >= 0) {
            // If there was a wake_up sensor_event, reset the flag.
            scratch[index_wake_up_event].flags &= ~WAKE_UP_SENSOR_EVENT_NEEDS_ACK;
            if (mWakeLockRefCount > 0) {
                --mWakeLockRefCount;
            }
#if DEBUG_CONNECTIONS
            --mTotalAcksNeeded;
#endif
        }
        if (mEventCache == nullptr) {
            mMaxCacheSize = computeMaxCacheSizeLocked();
            mEventCache = new sensors_event_t[mMaxCacheSize];
            mCacheSize = 0;
        }
        // Save the events so that they can be written later
        appendEventsToCacheLocked(scratch, count);

        // Add this file descriptor to the looper to get a callback when this fd is available for
        // writing.
        updateLooperRegistrationLocked(mService->getLooper());
        return size;
    }

#if DEBUG_CONNECTIONS
    if (size > 0) {
        mEventsSent += count;
    }
#endif

    return size < 0 ? status_t(size) : status_t(NO_ERROR);
}

bool SensorService::SensorEventConnection::hasSensorAccess() {
    return mService->isUidActive(mUid)
        && !mService->mSensorPrivacyPolicy->isSensorPrivacyEnabled();
}

bool SensorService::SensorEventConnection::noteOpIfRequired(const sensors_event_t& event) {
    bool success = true;
    const auto iter = mHandleToAppOp.find(event.sensor);
    if (iter != mHandleToAppOp.end()) {
        if (mTargetSdk == kTargetSdkUnknown) {
            // getTargetSdkVersion returns -1 if it fails so this operation should only be run once
            // per connection and then cached. Perform this here as opposed to in the constructor to
            // avoid log spam for NDK/VNDK clients that don't use sensors guarded with permissions
            // and pass in invalid op package names.
            mTargetSdk = SensorService::getTargetSdkVersion(mOpPackageName);
        }

        // Special handling for step count/detect backwards compatibility: if the app's target SDK
        // is pre-Q, still permit delivering events to the app even if permission isn't granted
        // (since this permission was only introduced in Q)
        if ((event.type == SENSOR_TYPE_STEP_COUNTER || event.type == SENSOR_TYPE_STEP_DETECTOR) &&
            mTargetSdk > 0 && mTargetSdk <= __ANDROID_API_P__) {
            success = true;
        } else if (mUid == AID_SYSTEM) {
            // Allow access if it is requested from system.
            success = true;
        } else {
            int32_t sensorHandle = event.sensor;
            String16 noteMsg("Sensor event (");
            noteMsg.append(String16(mService->getSensorStringType(sensorHandle)));
            noteMsg.append(String16(")"));
            int32_t appOpMode = mService->sAppOpsManager.noteOp(iter->second, mUid, mOpPackageName,
                                                                mAttributionTag, noteMsg);
            success = (appOpMode == AppOpsManager::MODE_ALLOWED);
        }
    }
    return success;
}

void SensorService::SensorEventConnection::reAllocateCacheLocked(sensors_event_t const* scratch,
                                                                 int count) {
    sensors_event_t *eventCache_new;
    const int new_cache_size = computeMaxCacheSizeLocked();
    // Allocate new cache, copy over events from the old cache & scratch, free up memory.
    eventCache_new = new sensors_event_t[new_cache_size];
    memcpy(eventCache_new, mEventCache, mCacheSize * sizeof(sensors_event_t));
    memcpy(&eventCache_new[mCacheSize], scratch, count * sizeof(sensors_event_t));

    ALOGD_IF(DEBUG_CONNECTIONS, "reAllocateCacheLocked maxCacheSize=%d %d", mMaxCacheSize,
            new_cache_size);

    delete[] mEventCache;
    mEventCache = eventCache_new;
    mCacheSize += count;
    mMaxCacheSize = new_cache_size;
}

void SensorService::SensorEventConnection::appendEventsToCacheLocked(sensors_event_t const* events,
                                                                     int count) {
    if (count <= 0) {
        return;
    } else if (mCacheSize + count <= mMaxCacheSize) {
        // The events fit within the current cache: add them
        memcpy(&mEventCache[mCacheSize], events, count * sizeof(sensors_event_t));
        mCacheSize += count;
    } else if (mCacheSize + count <= computeMaxCacheSizeLocked()) {
        // The events fit within a resized cache: resize the cache and add the events
        reAllocateCacheLocked(events, count);
    } else {
        // The events do not fit within the cache: drop the oldest events.
        int freeSpace = mMaxCacheSize - mCacheSize;

        // Drop up to the currently cached number of events to make room for new events
        int cachedEventsToDrop = std::min(mCacheSize, count - freeSpace);

        // New events need to be dropped if there are more new events than the size of the cache
        int newEventsToDrop = std::max(0, count - mMaxCacheSize);

        // Determine the number of new events to copy into the cache
        int eventsToCopy = std::min(mMaxCacheSize, count);

        constexpr nsecs_t kMinimumTimeBetweenDropLogNs = 2 * 1000 * 1000 * 1000; // 2 sec
        if (events[0].timestamp - mTimeOfLastEventDrop > kMinimumTimeBetweenDropLogNs) {
            ALOGW("Dropping %d cached events (%d/%d) to save %d/%d new events. %d events previously"
                    " dropped", cachedEventsToDrop, mCacheSize, mMaxCacheSize, eventsToCopy,
                    count, mEventsDropped);
            mEventsDropped = 0;
            mTimeOfLastEventDrop = events[0].timestamp;
        } else {
            // Record the number dropped
            mEventsDropped += cachedEventsToDrop + newEventsToDrop;
        }

        // Check for any flush complete events in the events that will be dropped
        countFlushCompleteEventsLocked(mEventCache, cachedEventsToDrop);
        countFlushCompleteEventsLocked(events, newEventsToDrop);

        // Only shift the events if they will not all be overwritten
        if (eventsToCopy != mMaxCacheSize) {
            memmove(mEventCache, &mEventCache[cachedEventsToDrop],
                    (mCacheSize - cachedEventsToDrop) * sizeof(sensors_event_t));
        }
        mCacheSize -= cachedEventsToDrop;

        // Copy the events into the cache
        memcpy(&mEventCache[mCacheSize], &events[newEventsToDrop],
                eventsToCopy * sizeof(sensors_event_t));
        mCacheSize += eventsToCopy;
    }
}

void SensorService::SensorEventConnection::sendPendingFlushEventsLocked() {
    ASensorEvent flushCompleteEvent;
    memset(&flushCompleteEvent, 0, sizeof(flushCompleteEvent));
    flushCompleteEvent.type = SENSOR_TYPE_META_DATA;
    // Loop through all the sensors for this connection and check if there are any pending
    // flush complete events to be sent.
    for (auto& it : mSensorInfo) {
        const int handle = it.first;
        std::shared_ptr<SensorInterface> si = mService->getSensorInterfaceFromHandle(handle);
        if (si == nullptr) {
            continue;
        }

        FlushInfo& flushInfo = it.second;
        while (flushInfo.mPendingFlushEventsToSend > 0) {
            flushCompleteEvent.meta_data.sensor = handle;
            bool wakeUpSensor = si->getSensor().isWakeUpSensor();
            if (wakeUpSensor) {
               ++mWakeLockRefCount;
               flushCompleteEvent.flags |= WAKE_UP_SENSOR_EVENT_NEEDS_ACK;
            }
            ssize_t size = SensorEventQueue::write(mChannel, &flushCompleteEvent, 1);
            if (size < 0) {
                if (wakeUpSensor) --mWakeLockRefCount;
                return;
            }
            ALOGD_IF(DEBUG_CONNECTIONS, "sent dropped flush complete event==%d ",
                    flushCompleteEvent.meta_data.sensor);
            flushInfo.mPendingFlushEventsToSend--;
        }
    }
}

void SensorService::SensorEventConnection::writeToSocketFromCache() {
    // At a time write at most half the size of the receiver buffer in SensorEventQueue OR
    // half the size of the socket buffer allocated in BitTube whichever is smaller.
    const int maxWriteSize = helpers::min(SensorEventQueue::MAX_RECEIVE_BUFFER_EVENT_COUNT/2,
            int(mService->mSocketBufferSize/(sizeof(sensors_event_t)*2)));
    Mutex::Autolock _l(mConnectionLock);
    // Send pending flush complete events (if any)
    sendPendingFlushEventsLocked();
    for (int numEventsSent = 0; numEventsSent < mCacheSize;) {
        const int numEventsToWrite = helpers::min(mCacheSize - numEventsSent, maxWriteSize);
        int index_wake_up_event = -1;
        if (hasSensorAccess()) {
            index_wake_up_event =
                      findWakeUpSensorEventLocked(mEventCache + numEventsSent, numEventsToWrite);
            if (index_wake_up_event >= 0) {
                mEventCache[index_wake_up_event + numEventsSent].flags |=
                        WAKE_UP_SENSOR_EVENT_NEEDS_ACK;
                ++mWakeLockRefCount;
#if DEBUG_CONNECTIONS
                ++mTotalAcksNeeded;
#endif
            }
        }

        ssize_t size = SensorEventQueue::write(mChannel,
                          reinterpret_cast<ASensorEvent const*>(mEventCache + numEventsSent),
                          numEventsToWrite);
        if (size < 0) {
            if (index_wake_up_event >= 0) {
                // If there was a wake_up sensor_event, reset the flag.
                mEventCache[index_wake_up_event + numEventsSent].flags  &=
                        ~WAKE_UP_SENSOR_EVENT_NEEDS_ACK;
                if (mWakeLockRefCount > 0) {
                    --mWakeLockRefCount;
                }
#if DEBUG_CONNECTIONS
                --mTotalAcksNeeded;
#endif
            }
            memmove(mEventCache, &mEventCache[numEventsSent],
                                 (mCacheSize - numEventsSent) * sizeof(sensors_event_t));
            ALOGD_IF(DEBUG_CONNECTIONS, "wrote %d events from cache size==%d ",
                    numEventsSent, mCacheSize);
            mCacheSize -= numEventsSent;
            return;
        }
        numEventsSent += numEventsToWrite;
#if DEBUG_CONNECTIONS
        mEventsSentFromCache += numEventsToWrite;
#endif
    }
    ALOGD_IF(DEBUG_CONNECTIONS, "wrote all events from cache size=%d ", mCacheSize);
    // All events from the cache have been sent. Reset cache size to zero.
    mCacheSize = 0;
    // There are no more events in the cache. We don't need to poll for write on the fd.
    // Update Looper registration.
    updateLooperRegistrationLocked(mService->getLooper());
}

void SensorService::SensorEventConnection::countFlushCompleteEventsLocked(
                sensors_event_t const* scratch, const int numEventsDropped) {
    ALOGD_IF(DEBUG_CONNECTIONS, "dropping %d events ", numEventsDropped);
    // Count flushComplete events in the events that are about to the dropped. These will be sent
    // separately before the next batch of events.
    for (int j = 0; j < numEventsDropped; ++j) {
        if (scratch[j].type == SENSOR_TYPE_META_DATA) {
            if (mSensorInfo.count(scratch[j].meta_data.sensor) == 0) {
                ALOGW("%s: sensor 0x%x is not found in connection",
                      __func__, scratch[j].meta_data.sensor);
                continue;
            }

            FlushInfo& flushInfo = mSensorInfo[scratch[j].meta_data.sensor];
            flushInfo.mPendingFlushEventsToSend++;
            ALOGD_IF(DEBUG_CONNECTIONS, "increment pendingFlushCount %d",
                     flushInfo.mPendingFlushEventsToSend);
        }
    }
    return;
}

int SensorService::SensorEventConnection::findWakeUpSensorEventLocked(
                       sensors_event_t const* scratch, const int count) {
    for (int i = 0; i < count; ++i) {
        if (mService->isWakeUpSensorEvent(scratch[i])) {
            return i;
        }
    }
    return -1;
}

sp<BitTube> SensorService::SensorEventConnection::getSensorChannel() const
{
    return mChannel;
}

status_t SensorService::SensorEventConnection::enableDisable(
        int handle, bool enabled, nsecs_t samplingPeriodNs, nsecs_t maxBatchReportLatencyNs,
        int reservedFlags)
{
    if (mDestroyed) {
        android_errorWriteLog(0x534e4554, "168211968");
        return DEAD_OBJECT;
    }

    status_t err;
    if (enabled) {
        nsecs_t requestedSamplingPeriodNs = samplingPeriodNs;
        bool isSensorCapped = false;
        std::shared_ptr<SensorInterface> si = mService->getSensorInterfaceFromHandle(handle);
        if (si != nullptr) {
            const Sensor& s = si->getSensor();
            if (mService->isSensorInCappedSet(s.getType())) {
                isSensorCapped = true;
            }
        }
        if (isSensorCapped) {
            err = mService->adjustSamplingPeriodBasedOnMicAndPermission(&samplingPeriodNs,
                                String16(mOpPackageName));
            if (err != OK) {
                return err;
            }
        }
        err = mService->enable(this, handle, samplingPeriodNs, maxBatchReportLatencyNs,
                               reservedFlags, mOpPackageName);
        if (err == OK && isSensorCapped) {
            if ((requestedSamplingPeriodNs >= SENSOR_SERVICE_CAPPED_SAMPLING_PERIOD_NS) ||
                !isRateCappedBasedOnPermission()) {
                Mutex::Autolock _l(mConnectionLock);
                mMicSamplingPeriodBackup[handle] = requestedSamplingPeriodNs;
            } else {
                Mutex::Autolock _l(mConnectionLock);
                mMicSamplingPeriodBackup[handle] = SENSOR_SERVICE_CAPPED_SAMPLING_PERIOD_NS;
            }
        }

    } else {
        err = mService->disable(this, handle);
        Mutex::Autolock _l(mConnectionLock);
        mMicSamplingPeriodBackup.erase(handle);
    }
    return err;
}

status_t SensorService::SensorEventConnection::setEventRate(int handle, nsecs_t samplingPeriodNs) {
    if (mDestroyed) {
        android_errorWriteLog(0x534e4554, "168211968");
        return DEAD_OBJECT;
    }

    nsecs_t requestedSamplingPeriodNs = samplingPeriodNs;
    bool isSensorCapped = false;
    std::shared_ptr<SensorInterface> si = mService->getSensorInterfaceFromHandle(handle);
    if (si != nullptr) {
        const Sensor& s = si->getSensor();
        if (mService->isSensorInCappedSet(s.getType())) {
            isSensorCapped = true;
        }
    }
    if (isSensorCapped) {
        status_t err = mService->adjustSamplingPeriodBasedOnMicAndPermission(&samplingPeriodNs,
                            String16(mOpPackageName));
        if (err != OK) {
            return err;
        }
    }
    status_t ret = mService->setEventRate(this, handle, samplingPeriodNs, mOpPackageName);
    if (ret == OK && isSensorCapped) {
        if ((requestedSamplingPeriodNs >= SENSOR_SERVICE_CAPPED_SAMPLING_PERIOD_NS) ||
            !isRateCappedBasedOnPermission()) {
            Mutex::Autolock _l(mConnectionLock);
            mMicSamplingPeriodBackup[handle] = requestedSamplingPeriodNs;
        } else {
            Mutex::Autolock _l(mConnectionLock);
            mMicSamplingPeriodBackup[handle] = SENSOR_SERVICE_CAPPED_SAMPLING_PERIOD_NS;
        }
    }
    return ret;
}

void SensorService::SensorEventConnection::onMicSensorAccessChanged(bool isMicToggleOn) {
    if (isMicToggleOn) {
        capRates();
    } else {
        uncapRates();
    }
}

void SensorService::SensorEventConnection::capRates() {
    Mutex::Autolock _l(mConnectionLock);
    SensorDevice& dev(SensorDevice::getInstance());
    for (auto &i : mMicSamplingPeriodBackup) {
        int handle = i.first;
        nsecs_t samplingPeriodNs = i.second;
        if (samplingPeriodNs < SENSOR_SERVICE_CAPPED_SAMPLING_PERIOD_NS) {
            if (hasSensorAccess()) {
                mService->setEventRate(this, handle, SENSOR_SERVICE_CAPPED_SAMPLING_PERIOD_NS,
                                       mOpPackageName);
            } else {
                // Update SensorDevice with the capped rate so that when sensor access is restored,
                // the correct event rate is used.
                dev.onMicSensorAccessChanged(this, handle,
                                             SENSOR_SERVICE_CAPPED_SAMPLING_PERIOD_NS);
            }
        }
    }
}

void SensorService::SensorEventConnection::uncapRates() {
    Mutex::Autolock _l(mConnectionLock);
    SensorDevice& dev(SensorDevice::getInstance());
    for (auto &i : mMicSamplingPeriodBackup) {
        int handle = i.first;
        nsecs_t samplingPeriodNs = i.second;
        if (samplingPeriodNs < SENSOR_SERVICE_CAPPED_SAMPLING_PERIOD_NS) {
            if (hasSensorAccess()) {
                mService->setEventRate(this, handle, samplingPeriodNs, mOpPackageName);
            } else {
                // Update SensorDevice with the uncapped rate so that when sensor access is
                // restored, the correct event rate is used.
                dev.onMicSensorAccessChanged(this, handle, samplingPeriodNs);
            }
        }
    }
}

status_t  SensorService::SensorEventConnection::flush() {
    if (mDestroyed) {
        return DEAD_OBJECT;
    }

    return  mService->flushSensor(this, mOpPackageName);
}

int32_t SensorService::SensorEventConnection::configureChannel(int handle, int rateLevel) {
    // SensorEventConnection does not support configureChannel, parameters not used
    UNUSED(handle);
    UNUSED(rateLevel);
    return INVALID_OPERATION;
}

int SensorService::SensorEventConnection::handleEvent(int fd, int events, void* /*data*/) {
    if (events & ALOOPER_EVENT_HANGUP || events & ALOOPER_EVENT_ERROR) {
        {
            // If the Looper encounters some error, set the flag mDead, reset mWakeLockRefCount,
            // and remove the fd from Looper. Call checkWakeLockState to know if SensorService
            // can release the wake-lock.
            ALOGD_IF(DEBUG_CONNECTIONS, "%p Looper error %d", this, fd);
            Mutex::Autolock _l(mConnectionLock);
            mDead = true;
            mWakeLockRefCount = 0;
            updateLooperRegistrationLocked(mService->getLooper());
        }
        mService->checkWakeLockState();
        if (mDataInjectionMode) {
            // If the Looper has encountered some error in data injection mode, reset SensorService
            // back to normal mode.
            mService->resetToNormalMode();
            mDataInjectionMode = false;
        }
        return 1;
    }

    if (events & ALOOPER_EVENT_INPUT) {
        unsigned char buf[sizeof(sensors_event_t)];
        ssize_t numBytesRead = ::recv(fd, buf, sizeof(buf), MSG_DONTWAIT);
        {
            Mutex::Autolock _l(mConnectionLock);
            if (numBytesRead == sizeof(sensors_event_t)) {
                if (!mDataInjectionMode) {
                    ALOGE("Data injected in normal mode, dropping event"
                          "package=%s uid=%d", mPackageName.c_str(), mUid);
                    // Unregister call backs.
                    return 0;
                }
                if (!mService->isAllowListedPackage(mPackageName)) {
                    ALOGE("App not allowed to inject data, dropping event"
                          "package=%s uid=%d", mPackageName.c_str(), mUid);
                    return 0;
                }
                sensors_event_t sensor_event;
                memcpy(&sensor_event, buf, sizeof(sensors_event_t));
                std::shared_ptr<SensorInterface> si =
                        mService->getSensorInterfaceFromHandle(sensor_event.sensor);
                if (si == nullptr) {
                    return 1;
                }

                SensorDevice& dev(SensorDevice::getInstance());
                sensor_event.type = si->getSensor().getType();
                dev.injectSensorData(&sensor_event);
#if DEBUG_CONNECTIONS
                ++mEventsReceived;
#endif
            } else if (numBytesRead == sizeof(uint32_t)) {
                uint32_t numAcks = 0;
                memcpy(&numAcks, buf, numBytesRead);
                // Check to ensure  there are no read errors in recv, numAcks is always
                // within the range and not zero. If any of the above don't hold reset
                // mWakeLockRefCount to zero.
                if (numAcks > 0 && numAcks < mWakeLockRefCount) {
                    mWakeLockRefCount -= numAcks;
                } else {
                    mWakeLockRefCount = 0;
                }
#if DEBUG_CONNECTIONS
                mTotalAcksReceived += numAcks;
#endif
           } else {
               // Read error, reset wakelock refcount.
               mWakeLockRefCount = 0;
           }
        }
        // Check if wakelock can be released by sensorservice. mConnectionLock needs to be released
        // here as checkWakeLockState() will need it.
        if (mWakeLockRefCount == 0) {
            mService->checkWakeLockState();
        }
        // continue getting callbacks.
        return 1;
    }

    if (events & ALOOPER_EVENT_OUTPUT) {
        // send sensor data that is stored in mEventCache for this connection.
        mService->sendEventsFromCache(this);
    }
    return 1;
}

int SensorService::SensorEventConnection::computeMaxCacheSizeLocked() const {
    size_t fifoWakeUpSensors = 0;
    size_t fifoNonWakeUpSensors = 0;
    for (auto& it : mSensorInfo) {
        std::shared_ptr<SensorInterface> si = mService->getSensorInterfaceFromHandle(it.first);
        if (si == nullptr) {
            continue;
        }
        const Sensor& sensor = si->getSensor();
        if (sensor.getFifoReservedEventCount() == sensor.getFifoMaxEventCount()) {
            // Each sensor has a reserved fifo. Sum up the fifo sizes for all wake up sensors and
            // non wake_up sensors.
            if (sensor.isWakeUpSensor()) {
                fifoWakeUpSensors += sensor.getFifoReservedEventCount();
            } else {
                fifoNonWakeUpSensors += sensor.getFifoReservedEventCount();
            }
        } else {
            // Shared fifo. Compute the max of the fifo sizes for wake_up and non_wake up sensors.
            if (sensor.isWakeUpSensor()) {
                fifoWakeUpSensors = fifoWakeUpSensors > sensor.getFifoMaxEventCount() ?
                                          fifoWakeUpSensors : sensor.getFifoMaxEventCount();

            } else {
                fifoNonWakeUpSensors = fifoNonWakeUpSensors > sensor.getFifoMaxEventCount() ?
                                          fifoNonWakeUpSensors : sensor.getFifoMaxEventCount();

            }
        }
   }
   if (fifoWakeUpSensors + fifoNonWakeUpSensors == 0) {
       // It is extremely unlikely that there is a write failure in non batch mode. Return a cache
       // size that is equal to that of the batch mode.
       // ALOGW("Write failure in non-batch mode");
       return MAX_SOCKET_BUFFER_SIZE_BATCHED/sizeof(sensors_event_t);
   }
   return fifoWakeUpSensors + fifoNonWakeUpSensors;
}

} // namespace android

