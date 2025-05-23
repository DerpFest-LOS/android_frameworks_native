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
#define FUZZ_LOG_TAG "binder_ndk"

#include "binder_ndk.h"
#include "aidl/parcelables/EmptyParcelable.h"
#include "aidl/parcelables/GenericDataParcelable.h"
#include "aidl/parcelables/SingleDataParcelable.h"

#include <android/binder_libbinder.h>
#include <android/binder_parcel_utils.h>
#include <android/binder_parcelable_utils.h>
#include <fuzzbinder/random_binder.h>
#include <fuzzbinder/random_fd.h>

#include "util.h"

// TODO(b/142061461): parent class
class SomeParcelable {
public:
    binder_status_t writeToParcel(AParcel* /*parcel*/) { return STATUS_OK; }
    binder_status_t readFromParcel(const AParcel* parcel) {
        return AParcel_readInt32(parcel, &mValue);
    }

private:
    int32_t mValue = 0;
};

class ISomeInterface : public ::ndk::ICInterface {
public:
    ISomeInterface() = default;
    virtual ~ISomeInterface() = default;
    static binder_status_t readFromParcel(const AParcel* parcel,
                                          std::shared_ptr<ISomeInterface>* instance);
};

static binder_status_t onTransact(AIBinder*, transaction_code_t, const AParcel*, AParcel*) {
    return STATUS_UNKNOWN_TRANSACTION;
}

static AIBinder_Class* g_class =
        ::ndk::ICInterface::defineClass("ISomeInterface", onTransact, nullptr, 0);

class BpSomeInterface : public ::ndk::BpCInterface<ISomeInterface> {
public:
    explicit BpSomeInterface(const ::ndk::SpAIBinder& binder) : BpCInterface(binder) {}
    virtual ~BpSomeInterface() = default;
};

binder_status_t ISomeInterface::readFromParcel(const AParcel* parcel,
                                               std::shared_ptr<ISomeInterface>* instance) {
    ::ndk::SpAIBinder binder;
    binder_status_t status = AParcel_readStrongBinder(parcel, binder.getR());
    if (status == STATUS_OK) {
        if (AIBinder_associateClass(binder.get(), g_class)) {
            *instance = std::static_pointer_cast<ISomeInterface>(
                    ::ndk::ICInterface::asInterface(binder.get()));
        } else {
            *instance = ::ndk::SharedRefBase::make<BpSomeInterface>(binder);
        }
    }
    return status;
}

#define PARCEL_READ(T, FUN)                                              \
    [](const NdkParcelAdapter& p, FuzzedDataProvider& /*provider*/) {    \
        FUZZ_LOG() << "about to read " #T " using " #FUN " with status"; \
        T t{};                                                           \
        binder_status_t status = FUN(p.aParcel(), &t);                   \
        FUZZ_LOG() << #T " status: " << status /* << " value: " << t*/;  \
    }

// clang-format off
std::vector<ParcelRead<NdkParcelAdapter>> BINDER_NDK_PARCEL_READ_FUNCTIONS{
        // methods from binder_parcel.h
        [](const NdkParcelAdapter& p, FuzzedDataProvider& provider) {
            // aborts on larger values
            size_t pos = provider.ConsumeIntegralInRange<size_t>(0, INT32_MAX);
            FUZZ_LOG() << "about to set data position to " << pos;
            binder_status_t status = AParcel_setDataPosition(p.aParcel(), pos);
            FUZZ_LOG() << "set data position: " << status;
        },
        [](const NdkParcelAdapter& p, FuzzedDataProvider& /*provider*/) {
            FUZZ_LOG() << "about to read status header";
            ndk::ScopedAStatus t;
            binder_status_t status = AParcel_readStatusHeader(p.aParcel(), t.getR());
            FUZZ_LOG() << "read status header: " << status;
        },
        [](const NdkParcelAdapter& p, FuzzedDataProvider& /*provider*/) {
            FUZZ_LOG() << "about to getDataSize the parcel";
            AParcel_getDataSize(p.aParcel());
            FUZZ_LOG() << "getDataSize done";
        },
        [](const NdkParcelAdapter& p, FuzzedDataProvider& provider) {
            FUZZ_LOG() << "about to read a ParcelableHolder";
            ndk::AParcelableHolder ph {provider.ConsumeBool() ? ndk::STABILITY_LOCAL : ndk::STABILITY_VINTF};
            binder_status_t status = AParcel_readParcelable(p.aParcel(), &ph);
            FUZZ_LOG() << "read the ParcelableHolder: " << status;
        },
        [](const NdkParcelAdapter& p, FuzzedDataProvider& provider) {
            size_t offset = provider.ConsumeIntegral<size_t>();
            size_t pos = provider.ConsumeIntegral<size_t>();
            FUZZ_LOG() << "about to appendFrom " << pos;
            // TODO: create random parcel
            AParcel* parcel = AParcel_create();
            binder_status_t status = AParcel_appendFrom(p.aParcel(), parcel, offset, pos);
            AParcel_delete(parcel);
            FUZZ_LOG() << "appendFrom: " << status;
        },

        PARCEL_READ(int32_t, AParcel_readInt32),
        PARCEL_READ(uint32_t, AParcel_readUint32),
        PARCEL_READ(int64_t, AParcel_readInt64),
        PARCEL_READ(uint64_t, AParcel_readUint64),
        PARCEL_READ(float, AParcel_readFloat),
        PARCEL_READ(double, AParcel_readDouble),
        PARCEL_READ(bool, AParcel_readBool),
        PARCEL_READ(char16_t, AParcel_readChar),
        PARCEL_READ(int8_t, AParcel_readByte),

        // methods from binder_parcel_utils.h
        PARCEL_READ(ndk::SpAIBinder, ndk::AParcel_readNullableStrongBinder),
        PARCEL_READ(ndk::SpAIBinder, ndk::AParcel_readRequiredStrongBinder),
        PARCEL_READ(ndk::ScopedFileDescriptor, ndk::AParcel_readNullableParcelFileDescriptor),
        PARCEL_READ(ndk::ScopedFileDescriptor, ndk::AParcel_readRequiredParcelFileDescriptor),
        PARCEL_READ(std::string, ndk::AParcel_readString),
        PARCEL_READ(std::optional<std::string>, ndk::AParcel_readString),

        PARCEL_READ(std::vector<std::string>, ndk::AParcel_readVector),
        PARCEL_READ(std::optional<std::vector<std::optional<std::string>>>, ndk::AParcel_readVector),
        PARCEL_READ(std::vector<SomeParcelable>, ndk::AParcel_readVector),
        PARCEL_READ(std::optional<std::vector<std::optional<SomeParcelable>>>, ndk::AParcel_readVector),
        PARCEL_READ(std::vector<ndk::SpAIBinder>, ndk::AParcel_readVector),
        PARCEL_READ(std::optional<std::vector<ndk::SpAIBinder>>, ndk::AParcel_readVector),
        PARCEL_READ(std::vector<ndk::ScopedFileDescriptor>, ndk::AParcel_readVector),
        PARCEL_READ(std::optional<std::vector<ndk::ScopedFileDescriptor>>, ndk::AParcel_readVector),
        PARCEL_READ(std::vector<std::shared_ptr<ISomeInterface>>, ndk::AParcel_readVector),
        PARCEL_READ(std::optional<std::vector<std::shared_ptr<ISomeInterface>>>, ndk::AParcel_readVector),
        PARCEL_READ(std::vector<int32_t>, ndk::AParcel_readVector),
        PARCEL_READ(std::optional<std::vector<int32_t>>, ndk::AParcel_readVector),
        PARCEL_READ(std::vector<uint32_t>, ndk::AParcel_readVector),
        PARCEL_READ(std::optional<std::vector<uint32_t>>, ndk::AParcel_readVector),
        PARCEL_READ(std::vector<int64_t>, ndk::AParcel_readVector),
        PARCEL_READ(std::optional<std::vector<int64_t>>, ndk::AParcel_readVector),
        PARCEL_READ(std::vector<uint64_t>, ndk::AParcel_readVector),
        PARCEL_READ(std::optional<std::vector<uint64_t>>, ndk::AParcel_readVector),
        PARCEL_READ(std::vector<float>, ndk::AParcel_readVector),
        PARCEL_READ(std::optional<std::vector<float>>, ndk::AParcel_readVector),
        PARCEL_READ(std::vector<double>, ndk::AParcel_readVector),
        PARCEL_READ(std::optional<std::vector<double>>, ndk::AParcel_readVector),
        PARCEL_READ(std::vector<bool>, ndk::AParcel_readVector),
        PARCEL_READ(std::optional<std::vector<bool>>, ndk::AParcel_readVector),
        PARCEL_READ(std::vector<char16_t>, ndk::AParcel_readVector),
        PARCEL_READ(std::optional<std::vector<char16_t>>, ndk::AParcel_readVector),
        PARCEL_READ(std::vector<int32_t>, ndk::AParcel_resizeVector),
        PARCEL_READ(std::optional<std::vector<int32_t>>, ndk::AParcel_resizeVector),

        // methods for std::array<T,N>
#define COMMA ,
        PARCEL_READ(std::array<bool COMMA 3>, ndk::AParcel_readData),
        PARCEL_READ(std::array<uint8_t COMMA 3>, ndk::AParcel_readData),
        PARCEL_READ(std::array<char16_t COMMA 3>, ndk::AParcel_readData),
        PARCEL_READ(std::array<int32_t COMMA 3>, ndk::AParcel_readData),
        PARCEL_READ(std::array<int64_t COMMA 3>, ndk::AParcel_readData),
        PARCEL_READ(std::array<float COMMA 3>, ndk::AParcel_readData),
        PARCEL_READ(std::array<double COMMA 3>, ndk::AParcel_readData),
        PARCEL_READ(std::array<std::string COMMA 3>, ndk::AParcel_readData),
        PARCEL_READ(std::array<SomeParcelable COMMA 3>, ndk::AParcel_readData),
        PARCEL_READ(std::array<ndk::SpAIBinder COMMA 3>, ndk::AParcel_readData),
        PARCEL_READ(std::array<ndk::ScopedFileDescriptor COMMA 3>, ndk::AParcel_readData),
        PARCEL_READ(std::array<std::shared_ptr<ISomeInterface> COMMA 3>, ndk::AParcel_readData),
#undef COMMA

        [](const NdkParcelAdapter& p, FuzzedDataProvider& /*provider*/) {
            FUZZ_LOG() << "about to read parcel using readFromParcel for EmptyParcelable";
            aidl::parcelables::EmptyParcelable emptyParcelable;
            binder_status_t status = emptyParcelable.readFromParcel(p.aParcel());
            FUZZ_LOG() << "status: " << status;
        },
        [](const NdkParcelAdapter& p, FuzzedDataProvider& /*provider*/) {
            FUZZ_LOG() << "about to read parcel using readFromParcel for SingleDataParcelable";
            aidl::parcelables::SingleDataParcelable singleDataParcelable;
            binder_status_t status = singleDataParcelable.readFromParcel(p.aParcel());
            FUZZ_LOG() << "status: " << status;
        },
        [](const NdkParcelAdapter& p, FuzzedDataProvider& /*provider*/) {
            FUZZ_LOG() << "about to read parcel using readFromParcel for GenericDataParcelable";
            aidl::parcelables::GenericDataParcelable genericDataParcelable;
            binder_status_t status = genericDataParcelable.readFromParcel(p.aParcel());
            FUZZ_LOG() << "status: " << status;
            std::string toString = genericDataParcelable.toString();
            FUZZ_LOG() << "toString() result: " << toString;
        },
        [](const NdkParcelAdapter& p, FuzzedDataProvider& provider) {
            FUZZ_LOG() << "about to marshal AParcel";
            size_t start = provider.ConsumeIntegral<size_t>();
            // limit 1MB to avoid OOM issues
            size_t len = provider.ConsumeIntegralInRange<size_t>(0, 1000000);
            uint8_t buffer[len];
            binder_status_t status = AParcel_marshal(p.aParcel(), buffer, start, len);
            FUZZ_LOG() << "status: " << status;
        },
};
std::vector<ParcelWrite<NdkParcelAdapter>> BINDER_NDK_PARCEL_WRITE_FUNCTIONS{
        [] (NdkParcelAdapter& p, FuzzedDataProvider& provider, android::RandomParcelOptions* options) {
            FUZZ_LOG() << "about to call AParcel_writeStrongBinder";

            // TODO: this logic is somewhat duplicated with random parcel
            android::sp<android::IBinder> binder;
            if (provider.ConsumeBool() && options->extraBinders.size() > 0) {
                binder = options->extraBinders.at(
                        provider.ConsumeIntegralInRange<size_t>(0, options->extraBinders.size() - 1));
            } else {
                binder = android::getRandomBinder(&provider);
                options->extraBinders.push_back(binder);
            }

            ndk::SpAIBinder abinder = ndk::SpAIBinder(AIBinder_fromPlatformBinder(binder));
            AParcel_writeStrongBinder(p.aParcel(), abinder.get());
        },
        [] (NdkParcelAdapter& p, FuzzedDataProvider& provider, android::RandomParcelOptions* options) {
            FUZZ_LOG() << "about to call AParcel_writeParcelFileDescriptor";

            auto fds = android::getRandomFds(&provider);
            if (fds.size() == 0) return;

            AParcel_writeParcelFileDescriptor(p.aParcel(), fds.at(0).get());
            options->extraFds.insert(options->extraFds.end(),
                 std::make_move_iterator(fds.begin() + 1),
                 std::make_move_iterator(fds.end()));
        },
        // all possible data writes can be done as a series of 4-byte reads
        [] (NdkParcelAdapter& p, FuzzedDataProvider& provider, android::RandomParcelOptions* /*options*/) {
            FUZZ_LOG() << "about to call AParcel_writeInt32";
            int32_t val = provider.ConsumeIntegral<int32_t>();
            AParcel_writeInt32(p.aParcel(), val);
        },
        [] (NdkParcelAdapter& p, FuzzedDataProvider& /* provider */, android::RandomParcelOptions* /*options*/) {
            FUZZ_LOG() << "about to call AParcel_reset";
            AParcel_reset(p.aParcel());
        },
        [](NdkParcelAdapter& p, FuzzedDataProvider& provider, android::RandomParcelOptions* /*options*/) {
            FUZZ_LOG() << "about to call AParcel_unmarshal";
            size_t len = provider.ConsumeIntegralInRange<size_t>(0, provider.remaining_bytes());
            std::vector<uint8_t> data = provider.ConsumeBytes<uint8_t>(len);
            binder_status_t status = AParcel_unmarshal(p.aParcel(), data.data(), data.size());
            FUZZ_LOG() << "status: " << status;
        },
};
// clang-format on
