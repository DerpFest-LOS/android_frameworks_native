/*
 * Copyright (C) 2020 The Android Open Source Project
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

#ifndef ANDROID_OS_VIBRATOR_MANAGER_HAL_WRAPPER_H
#define ANDROID_OS_VIBRATOR_MANAGER_HAL_WRAPPER_H

#include <aidl/android/hardware/vibrator/IVibratorManager.h>
#include <vibratorservice/VibratorHalController.h>
#include <unordered_map>

namespace android {

namespace vibrator {

// VibratorManager HAL capabilities.
enum class ManagerCapabilities : int32_t {
    NONE = 0,
    SYNC = aidl::android::hardware::vibrator::IVibratorManager::CAP_SYNC,
    PREPARE_ON = aidl::android::hardware::vibrator::IVibratorManager::CAP_PREPARE_ON,
    PREPARE_PERFORM = aidl::android::hardware::vibrator::IVibratorManager::CAP_PREPARE_PERFORM,
    PREPARE_COMPOSE = aidl::android::hardware::vibrator::IVibratorManager::CAP_PREPARE_COMPOSE,
    MIXED_TRIGGER_ON = aidl::android::hardware::vibrator::IVibratorManager::IVibratorManager::
            CAP_MIXED_TRIGGER_ON,
    MIXED_TRIGGER_PERFORM =
            aidl::android::hardware::vibrator::IVibratorManager::CAP_MIXED_TRIGGER_PERFORM,
    MIXED_TRIGGER_COMPOSE =
            aidl::android::hardware::vibrator::IVibratorManager::CAP_MIXED_TRIGGER_COMPOSE,
    TRIGGER_CALLBACK = aidl::android::hardware::vibrator::IVibratorManager::CAP_TRIGGER_CALLBACK,
    START_SESSIONS = aidl::android::hardware::vibrator::IVibratorManager::CAP_START_SESSIONS
};

inline ManagerCapabilities operator|(ManagerCapabilities lhs, ManagerCapabilities rhs) {
    using underlying = typename std::underlying_type<ManagerCapabilities>::type;
    return static_cast<ManagerCapabilities>(static_cast<underlying>(lhs) |
                                            static_cast<underlying>(rhs));
}

inline ManagerCapabilities& operator|=(ManagerCapabilities& lhs, ManagerCapabilities rhs) {
    return lhs = lhs | rhs;
}

inline ManagerCapabilities operator&(ManagerCapabilities lhs, ManagerCapabilities rhs) {
    using underlying = typename std::underlying_type<ManagerCapabilities>::type;
    return static_cast<ManagerCapabilities>(static_cast<underlying>(lhs) &
                                            static_cast<underlying>(rhs));
}

inline ManagerCapabilities& operator&=(ManagerCapabilities& lhs, ManagerCapabilities rhs) {
    return lhs = lhs & rhs;
}

// Wrapper for VibratorManager HAL handlers.
class ManagerHalWrapper {
public:
    using IVibrationSession = aidl::android::hardware::vibrator::IVibrationSession;
    using VibrationSessionConfig = aidl::android::hardware::vibrator::VibrationSessionConfig;

    ManagerHalWrapper() = default;
    virtual ~ManagerHalWrapper() = default;

    virtual HalResult<void> ping() = 0;

    /* reloads wrapped HAL service instance without waiting. This can be used to reconnect when the
     * service restarts, to rapidly retry after a failure.
     */
    virtual void tryReconnect() = 0;

    virtual HalResult<ManagerCapabilities> getCapabilities() = 0;
    virtual HalResult<std::vector<int32_t>> getVibratorIds() = 0;
    virtual HalResult<std::shared_ptr<HalController>> getVibrator(int32_t id) = 0;

    virtual HalResult<void> prepareSynced(const std::vector<int32_t>& ids);
    virtual HalResult<void> triggerSynced(const std::function<void()>& completionCallback);
    virtual HalResult<void> cancelSynced();
    virtual HalResult<std::shared_ptr<IVibrationSession>> startSession(
            const std::vector<int32_t>& ids, const VibrationSessionConfig& config,
            const std::function<void()>& completionCallback);
    virtual HalResult<void> clearSessions();
};

// Wrapper for the VibratorManager over single Vibrator HAL.
class LegacyManagerHalWrapper : public ManagerHalWrapper {
public:
    LegacyManagerHalWrapper() : LegacyManagerHalWrapper(std::make_shared<HalController>()) {}
    explicit LegacyManagerHalWrapper(std::shared_ptr<HalController> controller)
          : mController(std::move(controller)) {}
    virtual ~LegacyManagerHalWrapper() = default;

    HalResult<void> ping() override final;
    void tryReconnect() override final;

    HalResult<ManagerCapabilities> getCapabilities() override final;
    HalResult<std::vector<int32_t>> getVibratorIds() override final;
    HalResult<std::shared_ptr<HalController>> getVibrator(int32_t id) override final;

private:
    const std::shared_ptr<HalController> mController;
};

// Wrapper for the AIDL VibratorManager HAL.
class AidlManagerHalWrapper : public ManagerHalWrapper {
public:
    using VibratorManager = aidl::android::hardware::vibrator::IVibratorManager;

    explicit AidlManagerHalWrapper(std::shared_ptr<CallbackScheduler> callbackScheduler,
                                   std::shared_ptr<VibratorManager> handle)
          : mHandle(std::move(handle)), mCallbackScheduler(callbackScheduler) {}
    virtual ~AidlManagerHalWrapper() = default;

    HalResult<void> ping() override final;
    void tryReconnect() override final;

    HalResult<ManagerCapabilities> getCapabilities() override final;
    HalResult<std::vector<int32_t>> getVibratorIds() override final;
    HalResult<std::shared_ptr<HalController>> getVibrator(int32_t id) override final;

    HalResult<void> prepareSynced(const std::vector<int32_t>& ids) override final;
    HalResult<void> triggerSynced(const std::function<void()>& completionCallback) override final;
    HalResult<void> cancelSynced() override final;
    HalResult<std::shared_ptr<IVibrationSession>> startSession(
            const std::vector<int32_t>& ids, const VibrationSessionConfig& config,
            const std::function<void()>& completionCallback) override final;
    HalResult<void> clearSessions() override final;

private:
    std::mutex mHandleMutex;
    std::mutex mCapabilitiesMutex;
    std::mutex mVibratorsMutex;
    std::shared_ptr<VibratorManager> mHandle GUARDED_BY(mHandleMutex);
    std::optional<ManagerCapabilities> mCapabilities GUARDED_BY(mCapabilitiesMutex);
    std::optional<std::vector<int32_t>> mVibratorIds GUARDED_BY(mVibratorsMutex);
    std::unordered_map<int32_t, std::shared_ptr<HalController>> mVibrators
            GUARDED_BY(mVibratorsMutex);
    std::shared_ptr<CallbackScheduler> mCallbackScheduler;

    std::shared_ptr<VibratorManager> getHal();
    std::shared_ptr<HalWrapper> connectToVibrator(int32_t vibratorId,
                                                  std::shared_ptr<CallbackScheduler> scheduler);
};

}; // namespace vibrator

}; // namespace android

#endif // ANDROID_OS_VIBRATOR_MANAGER_HAL_WRAPPER_H
