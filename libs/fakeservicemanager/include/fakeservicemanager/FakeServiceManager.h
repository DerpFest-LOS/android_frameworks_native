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

#pragma once

#include <binder/IServiceManager.h>

#include <map>
#include <mutex>
#include <optional>
#include <vector>

namespace android {

/**
 * A local host simple implementation of IServiceManager, that does not
 * communicate over binder.
*/
class FakeServiceManager : public IServiceManager {
public:
    FakeServiceManager();

    sp<IBinder> getService( const String16& name) const override;

    sp<IBinder> checkService( const String16& name) const override;

    status_t addService(const String16& name, const sp<IBinder>& service,
                        bool allowIsolated = false,
                        int dumpsysFlags = DUMP_FLAG_PRIORITY_DEFAULT) override;

    Vector<String16> listServices(int dumpsysFlags = 0) override;

    IBinder* onAsBinder() override;

    sp<IBinder> waitForService(const String16& name) override;

    bool isDeclared(const String16& name) override;

    Vector<String16> getDeclaredInstances(const String16& iface) override;

    std::optional<String16> updatableViaApex(const String16& name) override;

    Vector<String16> getUpdatableNames(const String16& apexName) override;

    std::optional<IServiceManager::ConnectionInfo> getConnectionInfo(const String16& name) override;

    status_t registerForNotifications(const String16& name,
                                      const sp<LocalRegistrationCallback>& callback) override;

    status_t unregisterForNotifications(const String16& name,
                                        const sp<LocalRegistrationCallback>& callback) override;

    std::vector<IServiceManager::ServiceDebugInfo> getServiceDebugInfo() override;

    void enableAddServiceCache(bool /*value*/) override {}
    // Clear all of the registered services
    void clear();

private:
    mutable std::mutex mMutex;
    std::map<String16, sp<IBinder>> mNameToService;
};

}  // namespace android
