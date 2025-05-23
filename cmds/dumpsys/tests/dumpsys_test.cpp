/*
 * Copyright (C) 2016 The Android Open Source Project
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

#include "../dumpsys.h"

#include <regex>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <android-base/file.h>
#include <binder/Binder.h>
#include <binder/ProcessState.h>
#include <serviceutils/PriorityDumper.h>
#include <utils/String16.h>
#include <utils/String8.h>
#include <utils/Vector.h>

using namespace android;

using ::testing::_;
using ::testing::Action;
using ::testing::ActionInterface;
using ::testing::DoAll;
using ::testing::Eq;
using ::testing::HasSubstr;
using ::testing::MakeAction;
using ::testing::Mock;
using ::testing::Not;
using ::testing::Return;
using ::testing::StrEq;
using ::testing::Test;
using ::testing::WithArg;
using ::testing::internal::CaptureStderr;
using ::testing::internal::CaptureStdout;
using ::testing::internal::GetCapturedStderr;
using ::testing::internal::GetCapturedStdout;

class ServiceManagerMock : public IServiceManager {
  public:
    MOCK_CONST_METHOD1(getService, sp<IBinder>(const String16&));
    MOCK_CONST_METHOD1(checkService, sp<IBinder>(const String16&));
    MOCK_METHOD4(addService, status_t(const String16&, const sp<IBinder>&, bool, int));
    MOCK_METHOD1(listServices, Vector<String16>(int));
    MOCK_METHOD1(waitForService, sp<IBinder>(const String16&));
    MOCK_METHOD1(isDeclared, bool(const String16&));
    MOCK_METHOD1(getDeclaredInstances, Vector<String16>(const String16&));
    MOCK_METHOD1(updatableViaApex, std::optional<String16>(const String16&));
    MOCK_METHOD1(getUpdatableNames, Vector<String16>(const String16&));
    MOCK_METHOD1(getConnectionInfo, std::optional<ConnectionInfo>(const String16&));
    MOCK_METHOD2(registerForNotifications, status_t(const String16&,
                                             const sp<LocalRegistrationCallback>&));
    MOCK_METHOD2(unregisterForNotifications, status_t(const String16&,
                                             const sp<LocalRegistrationCallback>&));
    MOCK_METHOD0(getServiceDebugInfo, std::vector<ServiceDebugInfo>());
    MOCK_METHOD1(enableAddServiceCache, void(bool));

  protected:
    MOCK_METHOD0(onAsBinder, IBinder*());
};

class BinderMock : public BBinder {
  public:
    BinderMock() {
    }

    MOCK_METHOD2(dump, status_t(int, const Vector<String16>&));
};

// gmock black magic to provide a WithArg<0>(WriteOnFd(output)) matcher
typedef void WriteOnFdFunction(int);

class WriteOnFdAction : public ActionInterface<WriteOnFdFunction> {
  public:
    explicit WriteOnFdAction(const std::string& output) : output_(output) {
    }
    virtual Result Perform(const ArgumentTuple& args) {
        int fd = ::testing::get<0>(args);
        android::base::WriteStringToFd(output_, fd);
    }

  private:
    std::string output_;
};

// Matcher used to emulate dump() by writing on its file descriptor.
Action<WriteOnFdFunction> WriteOnFd(const std::string& output) {
    return MakeAction(new WriteOnFdAction(output));
}

// Matcher for args using Android's Vector<String16> format
// TODO: move it to some common testing library
MATCHER_P(AndroidElementsAre, expected, "") {
    std::ostringstream errors;
    if (arg.size() != expected.size()) {
        errors << " sizes do not match (expected " << expected.size() << ", got " << arg.size()
               << ")\n";
    }
    int i = 0;
    std::ostringstream actual_stream, expected_stream;
    for (const String16& actual : arg) {
        std::string actual_str = String8(actual).c_str();
        std::string expected_str = expected[i];
        actual_stream << "'" << actual_str << "' ";
        expected_stream << "'" << expected_str << "' ";
        if (actual_str != expected_str) {
            errors << " element mismatch at index " << i << "\n";
        }
        i++;
    }

    if (!errors.str().empty()) {
        errors << "\nExpected args: " << expected_stream.str()
               << "\nActual args: " << actual_stream.str();
        *result_listener << errors.str();
        return false;
    }
    return true;
}

// Custom action to sleep for timeout seconds
ACTION_P(Sleep, timeout) {
    sleep(timeout);
}

class DumpsysTest : public Test {
  public:
    DumpsysTest() : sm_(), dump_(&sm_), stdout_(), stderr_() {
    }

    void ExpectListServices(std::vector<std::string> services) {
        Vector<String16> services16;
        for (auto& service : services) {
            services16.add(String16(service.c_str()));
        }
        EXPECT_CALL(sm_, listServices(IServiceManager::DUMP_FLAG_PRIORITY_ALL))
            .WillRepeatedly(Return(services16));
    }

    void ExpectListServicesWithPriority(std::vector<std::string> services, int dumpFlags) {
        Vector<String16> services16;
        for (auto& service : services) {
            services16.add(String16(service.c_str()));
        }
        EXPECT_CALL(sm_, listServices(dumpFlags)).WillRepeatedly(Return(services16));
    }

    sp<BinderMock> ExpectCheckService(const char* name, bool running = true) {
        sp<BinderMock> binder_mock;
        if (running) {
            binder_mock = new BinderMock;
        }
        EXPECT_CALL(sm_, checkService(String16(name))).WillRepeatedly(Return(binder_mock));
        return binder_mock;
    }

    void ExpectDump(const char* name, const std::string& output) {
        sp<BinderMock> binder_mock = ExpectCheckService(name);
        EXPECT_CALL(*binder_mock, dump(_, _))
            .WillRepeatedly(DoAll(WithArg<0>(WriteOnFd(output)), Return(0)));
    }

    void ExpectDumpWithArgs(const char* name, std::vector<std::string> args,
                            const std::string& output) {
        sp<BinderMock> binder_mock = ExpectCheckService(name);
        EXPECT_CALL(*binder_mock, dump(_, AndroidElementsAre(args)))
            .WillRepeatedly(DoAll(WithArg<0>(WriteOnFd(output)), Return(0)));
    }

    sp<BinderMock> ExpectDumpAndHang(const char* name, int timeout_s, const std::string& output) {
        sp<BinderMock> binder_mock = ExpectCheckService(name);
        EXPECT_CALL(*binder_mock, dump(_, _))
            .WillRepeatedly(DoAll(Sleep(timeout_s), WithArg<0>(WriteOnFd(output)), Return(0)));
        return binder_mock;
    }

    void CallMain(const std::vector<std::string>& args) {
        const char* argv[1024] = {"/some/virtual/dir/dumpsys"};
        int argc = (int)args.size() + 1;
        int i = 1;
        for (const std::string& arg : args) {
            argv[i++] = arg.c_str();
        }
        CaptureStdout();
        CaptureStderr();
        int status = dump_.main(argc, const_cast<char**>(argv));
        stdout_ = GetCapturedStdout();
        stderr_ = GetCapturedStderr();
        EXPECT_THAT(status, Eq(0));
    }

    void CallSingleService(const String16& serviceName, Vector<String16>& args, int priorityFlags,
                           bool supportsProto, std::chrono::duration<double>& elapsedDuration,
                           size_t& bytesWritten) {
        CaptureStdout();
        CaptureStderr();
        dump_.setServiceArgs(args, supportsProto, priorityFlags);
        status_t status = dump_.startDumpThread(Dumpsys::TYPE_DUMP, serviceName, args);
        EXPECT_THAT(status, Eq(0));
        status = dump_.writeDump(STDOUT_FILENO, serviceName, std::chrono::milliseconds(500), false,
                                 elapsedDuration, bytesWritten);
        EXPECT_THAT(status, Eq(0));
        dump_.stopDumpThread(/* dumpCompleted = */ true);
        stdout_ = GetCapturedStdout();
        stderr_ = GetCapturedStderr();
    }

    void AssertRunningServices(const std::vector<std::string>& services) {
        std::string expected = "Currently running services:\n";
        for (const std::string& service : services) {
            expected.append("  ").append(service).append("\n");
        }
        EXPECT_THAT(stdout_, HasSubstr(expected));
    }

    void AssertOutput(const std::string& expected) {
        EXPECT_THAT(stdout_, StrEq(expected));
    }

    void AssertOutputContains(const std::string& expected) {
        EXPECT_THAT(stdout_, HasSubstr(expected));
    }

    void AssertOutputFormat(const std::string format) {
        EXPECT_THAT(stdout_, testing::MatchesRegex(format));
    }

    void AssertDumped(const std::string& service, const std::string& dump) {
        EXPECT_THAT(stdout_, HasSubstr("DUMP OF SERVICE " + service + ":\n" + dump));
        EXPECT_THAT(stdout_, HasSubstr("was the duration of dumpsys " + service + ", ending at: "));
    }

    void AssertDumpedWithPriority(const std::string& service, const std::string& dump,
                                  const char16_t* priorityType) {
        std::string priority = String8(priorityType).c_str();
        EXPECT_THAT(stdout_,
                    HasSubstr("DUMP OF SERVICE " + priority + " " + service + ":\n" + dump));
        EXPECT_THAT(stdout_, HasSubstr("was the duration of dumpsys " + service + ", ending at: "));
    }

    void AssertNotDumped(const std::string& dump) {
        EXPECT_THAT(stdout_, Not(HasSubstr(dump)));
    }

    void AssertStopped(const std::string& service) {
        EXPECT_THAT(stderr_, HasSubstr("Can't find service: " + service + "\n"));
    }

    ServiceManagerMock sm_;
    Dumpsys dump_;

  private:
    std::string stdout_, stderr_;
};

// Tests 'dumpsys -l' when all services are running
TEST_F(DumpsysTest, ListAllServices) {
    ExpectListServices({"Locksmith", "Valet"});
    ExpectCheckService("Locksmith");
    ExpectCheckService("Valet");

    CallMain({"-l"});

    AssertRunningServices({"Locksmith", "Valet"});
}

TEST_F(DumpsysTest, ListServicesOneRegistered) {
    ExpectListServices({"Locksmith"});
    ExpectCheckService("Locksmith");

    CallMain({"-l"});

    AssertRunningServices({"Locksmith"});
}

TEST_F(DumpsysTest, ListServicesEmpty) {
    CallMain({"-l"});

    AssertRunningServices({});
}

// Tests 'dumpsys -l' when a service is not running
TEST_F(DumpsysTest, ListRunningServices) {
    ExpectListServices({"Locksmith", "Valet"});
    ExpectCheckService("Locksmith");
    ExpectCheckService("Valet", false);

    CallMain({"-l"});

    AssertRunningServices({"Locksmith"});
    AssertNotDumped({"Valet"});
}

// Tests 'dumpsys -l --priority HIGH'
TEST_F(DumpsysTest, ListAllServicesWithPriority) {
    ExpectListServicesWithPriority({"Locksmith", "Valet"}, IServiceManager::DUMP_FLAG_PRIORITY_HIGH);
    ExpectCheckService("Locksmith");
    ExpectCheckService("Valet");

    CallMain({"-l", "--priority", "HIGH"});

    AssertRunningServices({"Locksmith", "Valet"});
}

// Tests 'dumpsys -l --priority HIGH' with and empty list
TEST_F(DumpsysTest, ListEmptyServicesWithPriority) {
    ExpectListServicesWithPriority({}, IServiceManager::DUMP_FLAG_PRIORITY_HIGH);

    CallMain({"-l", "--priority", "HIGH"});

    AssertRunningServices({});
}

// Tests 'dumpsys -l --proto'
TEST_F(DumpsysTest, ListAllServicesWithProto) {
    ExpectListServicesWithPriority({"Locksmith", "Valet", "Car"},
                                   IServiceManager::DUMP_FLAG_PRIORITY_ALL);
    ExpectListServicesWithPriority({"Valet", "Car"}, IServiceManager::DUMP_FLAG_PROTO);
    ExpectCheckService("Car");
    ExpectCheckService("Valet");

    CallMain({"-l", "--proto"});

    AssertRunningServices({"Car", "Valet"});
}

// Tests 'dumpsys service_name' on a service is running
TEST_F(DumpsysTest, DumpRunningService) {
    ExpectDump("Valet", "Here's your car");

    CallMain({"Valet"});

    AssertOutput("Here's your car");
}

// Tests 'dumpsys -t 1 service_name' on a service that times out after 2s
TEST_F(DumpsysTest, DumpRunningServiceTimeoutInSec) {
    sp<BinderMock> binder_mock = ExpectDumpAndHang("Valet", 2, "Here's your car");

    CallMain({"-t", "1", "Valet"});

    AssertOutputContains("SERVICE 'Valet' DUMP TIMEOUT (1000ms) EXPIRED");
    AssertNotDumped("Here's your car");

    // TODO(b/65056227): BinderMock is not destructed because thread is detached on dumpsys.cpp
    Mock::AllowLeak(binder_mock.get());
}

// Tests 'dumpsys -T 500 service_name' on a service that times out after 2s
TEST_F(DumpsysTest, DumpRunningServiceTimeoutInMs) {
    sp<BinderMock> binder_mock = ExpectDumpAndHang("Valet", 2, "Here's your car");

    CallMain({"-T", "500", "Valet"});

    AssertOutputContains("SERVICE 'Valet' DUMP TIMEOUT (500ms) EXPIRED");
    AssertNotDumped("Here's your car");

    // TODO(b/65056227): BinderMock is not destructed because thread is detached on dumpsys.cpp
    Mock::AllowLeak(binder_mock.get());
}

// Tests 'dumpsys service_name Y U NO HAVE ARGS' on a service that is running
TEST_F(DumpsysTest, DumpWithArgsRunningService) {
    ExpectDumpWithArgs("SERVICE", {"Y", "U", "NO", "HANDLE", "ARGS"}, "I DO!");

    CallMain({"SERVICE", "Y", "U", "NO", "HANDLE", "ARGS"});

    AssertOutput("I DO!");
}

// Tests dumpsys passes the -a flag when called on all services
TEST_F(DumpsysTest, PassAllFlagsToServices) {
    ExpectListServices({"Locksmith", "Valet"});
    ExpectCheckService("Locksmith");
    ExpectCheckService("Valet");
    ExpectDumpWithArgs("Locksmith", {"-a"}, "dumped1");
    ExpectDumpWithArgs("Valet", {"-a"}, "dumped2");

    CallMain({"-T", "500"});

    AssertDumped("Locksmith", "dumped1");
    AssertDumped("Valet", "dumped2");
}

// Tests dumpsys passes the -a flag when called on NORMAL priority services
TEST_F(DumpsysTest, PassAllFlagsToNormalServices) {
    ExpectListServicesWithPriority({"Locksmith", "Valet"},
                                   IServiceManager::DUMP_FLAG_PRIORITY_NORMAL);
    ExpectCheckService("Locksmith");
    ExpectCheckService("Valet");
    ExpectDumpWithArgs("Locksmith", {"--dump-priority", "NORMAL", "-a"}, "dump1");
    ExpectDumpWithArgs("Valet", {"--dump-priority", "NORMAL", "-a"}, "dump2");

    CallMain({"--priority", "NORMAL"});

    AssertDumped("Locksmith", "dump1");
    AssertDumped("Valet", "dump2");
}

// Tests dumpsys passes only priority flags when called on CRITICAL priority services
TEST_F(DumpsysTest, PassPriorityFlagsToCriticalServices) {
    ExpectListServicesWithPriority({"Locksmith", "Valet"},
                                   IServiceManager::DUMP_FLAG_PRIORITY_CRITICAL);
    ExpectCheckService("Locksmith");
    ExpectCheckService("Valet");
    ExpectDumpWithArgs("Locksmith", {"--dump-priority", "CRITICAL"}, "dump1");
    ExpectDumpWithArgs("Valet", {"--dump-priority", "CRITICAL"}, "dump2");

    CallMain({"--priority", "CRITICAL"});

    AssertDumpedWithPriority("Locksmith", "dump1", PriorityDumper::PRIORITY_ARG_CRITICAL);
    AssertDumpedWithPriority("Valet", "dump2", PriorityDumper::PRIORITY_ARG_CRITICAL);
}

// Tests dumpsys passes only priority flags when called on HIGH priority services
TEST_F(DumpsysTest, PassPriorityFlagsToHighServices) {
    ExpectListServicesWithPriority({"Locksmith", "Valet"},
                                   IServiceManager::DUMP_FLAG_PRIORITY_HIGH);
    ExpectCheckService("Locksmith");
    ExpectCheckService("Valet");
    ExpectDumpWithArgs("Locksmith", {"--dump-priority", "HIGH"}, "dump1");
    ExpectDumpWithArgs("Valet", {"--dump-priority", "HIGH"}, "dump2");

    CallMain({"--priority", "HIGH"});

    AssertDumpedWithPriority("Locksmith", "dump1", PriorityDumper::PRIORITY_ARG_HIGH);
    AssertDumpedWithPriority("Valet", "dump2", PriorityDumper::PRIORITY_ARG_HIGH);
}

// Tests 'dumpsys' with no arguments
TEST_F(DumpsysTest, DumpMultipleServices) {
    ExpectListServices({"running1", "stopped2", "running3"});
    ExpectDump("running1", "dump1");
    ExpectCheckService("stopped2", false);
    ExpectDump("running3", "dump3");

    CallMain({});

    AssertRunningServices({"running1", "running3"});
    AssertDumped("running1", "dump1");
    AssertStopped("stopped2");
    AssertDumped("running3", "dump3");
}

// Tests 'dumpsys --skip skipped3 skipped5', which should skip these services
TEST_F(DumpsysTest, DumpWithSkip) {
    ExpectListServices({"running1", "stopped2", "skipped3", "running4", "skipped5"});
    ExpectDump("running1", "dump1");
    ExpectCheckService("stopped2", false);
    ExpectDump("skipped3", "dump3");
    ExpectDump("running4", "dump4");
    ExpectDump("skipped5", "dump5");

    CallMain({"--skip", "skipped3", "skipped5"});

    AssertRunningServices({"running1", "running4", "skipped3 (skipped)", "skipped5 (skipped)"});
    AssertDumped("running1", "dump1");
    AssertDumped("running4", "dump4");
    AssertStopped("stopped2");
    AssertNotDumped("dump3");
    AssertNotDumped("dump5");
}

// Tests 'dumpsys --skip skipped3 skipped5 --priority CRITICAL', which should skip these services
TEST_F(DumpsysTest, DumpWithSkipAndPriority) {
    ExpectListServicesWithPriority({"running1", "stopped2", "skipped3", "running4", "skipped5"},
                                   IServiceManager::DUMP_FLAG_PRIORITY_CRITICAL);
    ExpectDump("running1", "dump1");
    ExpectCheckService("stopped2", false);
    ExpectDump("skipped3", "dump3");
    ExpectDump("running4", "dump4");
    ExpectDump("skipped5", "dump5");

    CallMain({"--priority", "CRITICAL", "--skip", "skipped3", "skipped5"});

    AssertRunningServices({"running1", "running4", "skipped3 (skipped)", "skipped5 (skipped)"});
    AssertDumpedWithPriority("running1", "dump1", PriorityDumper::PRIORITY_ARG_CRITICAL);
    AssertDumpedWithPriority("running4", "dump4", PriorityDumper::PRIORITY_ARG_CRITICAL);
    AssertStopped("stopped2");
    AssertNotDumped("dump3");
    AssertNotDumped("dump5");
}

// Tests 'dumpsys --priority CRITICAL'
TEST_F(DumpsysTest, DumpWithPriorityCritical) {
    ExpectListServicesWithPriority({"runningcritical1", "runningcritical2"},
                                   IServiceManager::DUMP_FLAG_PRIORITY_CRITICAL);
    ExpectDump("runningcritical1", "dump1");
    ExpectDump("runningcritical2", "dump2");

    CallMain({"--priority", "CRITICAL"});

    AssertRunningServices({"runningcritical1", "runningcritical2"});
    AssertDumpedWithPriority("runningcritical1", "dump1", PriorityDumper::PRIORITY_ARG_CRITICAL);
    AssertDumpedWithPriority("runningcritical2", "dump2", PriorityDumper::PRIORITY_ARG_CRITICAL);
}

// Tests 'dumpsys --priority HIGH'
TEST_F(DumpsysTest, DumpWithPriorityHigh) {
    ExpectListServicesWithPriority({"runninghigh1", "runninghigh2"},
                                   IServiceManager::DUMP_FLAG_PRIORITY_HIGH);
    ExpectDump("runninghigh1", "dump1");
    ExpectDump("runninghigh2", "dump2");

    CallMain({"--priority", "HIGH"});

    AssertRunningServices({"runninghigh1", "runninghigh2"});
    AssertDumpedWithPriority("runninghigh1", "dump1", PriorityDumper::PRIORITY_ARG_HIGH);
    AssertDumpedWithPriority("runninghigh2", "dump2", PriorityDumper::PRIORITY_ARG_HIGH);
}

// Tests 'dumpsys --priority NORMAL'
TEST_F(DumpsysTest, DumpWithPriorityNormal) {
    ExpectListServicesWithPriority({"runningnormal1", "runningnormal2"},
                                   IServiceManager::DUMP_FLAG_PRIORITY_NORMAL);
    ExpectDump("runningnormal1", "dump1");
    ExpectDump("runningnormal2", "dump2");

    CallMain({"--priority", "NORMAL"});

    AssertRunningServices({"runningnormal1", "runningnormal2"});
    AssertDumped("runningnormal1", "dump1");
    AssertDumped("runningnormal2", "dump2");
}

// Tests 'dumpsys --proto'
TEST_F(DumpsysTest, DumpWithProto) {
    ExpectListServicesWithPriority({"run8", "run1", "run2", "run5"},
                                   IServiceManager::DUMP_FLAG_PRIORITY_ALL);
    ExpectListServicesWithPriority({"run3", "run2", "run4", "run8"},
                                   IServiceManager::DUMP_FLAG_PROTO);
    ExpectDump("run2", "dump1");
    ExpectDump("run8", "dump2");

    CallMain({"--proto"});

    AssertRunningServices({"run2", "run8"});
    AssertDumped("run2", "dump1");
    AssertDumped("run8", "dump2");
}

// Tests 'dumpsys --priority HIGH --proto'
TEST_F(DumpsysTest, DumpWithPriorityHighAndProto) {
    ExpectListServicesWithPriority({"runninghigh1", "runninghigh2"},
                                   IServiceManager::DUMP_FLAG_PRIORITY_HIGH);
    ExpectListServicesWithPriority({"runninghigh1", "runninghigh2", "runninghigh3"},
                                   IServiceManager::DUMP_FLAG_PROTO);

    ExpectDump("runninghigh1", "dump1");
    ExpectDump("runninghigh2", "dump2");

    CallMain({"--priority", "HIGH", "--proto"});

    AssertRunningServices({"runninghigh1", "runninghigh2"});
    AssertDumpedWithPriority("runninghigh1", "dump1", PriorityDumper::PRIORITY_ARG_HIGH);
    AssertDumpedWithPriority("runninghigh2", "dump2", PriorityDumper::PRIORITY_ARG_HIGH);
}

// Tests 'dumpsys --pid'
TEST_F(DumpsysTest, ListAllServicesWithPid) {
    ExpectListServices({"Locksmith", "Valet"});
    ExpectCheckService("Locksmith");
    ExpectCheckService("Valet");

    CallMain({"--pid"});

    AssertRunningServices({"Locksmith", "Valet"});
    AssertOutputContains(std::to_string(getpid()));
}

// Tests 'dumpsys --pid service_name'
TEST_F(DumpsysTest, ListServiceWithPid) {
    ExpectCheckService("Locksmith");

    CallMain({"--pid", "Locksmith"});

    AssertOutput(std::to_string(getpid()) + "\n");
}

// Tests 'dumpsys --stability'
TEST_F(DumpsysTest, ListAllServicesWithStability) {
    ExpectListServices({"Locksmith", "Valet"});
    ExpectCheckService("Locksmith");
    ExpectCheckService("Valet");

    CallMain({"--stability"});

    AssertRunningServices({"Locksmith", "Valet"});
    AssertOutputContains("stability");
}

// Tests 'dumpsys --stability service_name'
TEST_F(DumpsysTest, ListServiceWithStability) {
    ExpectCheckService("Locksmith");

    CallMain({"--stability", "Locksmith"});

    AssertOutputContains("stability");
}

// Tests 'dumpsys --thread'
TEST_F(DumpsysTest, ListAllServicesWithThread) {
    ExpectListServices({"Locksmith", "Valet"});
    ExpectCheckService("Locksmith");
    ExpectCheckService("Valet");

    CallMain({"--thread"});

    AssertRunningServices({"Locksmith", "Valet"});

    const std::string format("(.|\n)*((Threads in use: [0-9]+/[0-9]+)?\n-(.|\n)*){2}");
    AssertOutputFormat(format);
}

// Tests 'dumpsys --thread service_name'
TEST_F(DumpsysTest, ListServiceWithThread) {
    ExpectCheckService("Locksmith");

    CallMain({"--thread", "Locksmith"});
    // returns an empty string without root enabled
    const std::string format("(^$|Threads in use: [0-9]/[0-9]+\n)");
    AssertOutputFormat(format);
}

// Tests 'dumpsys --clients'
TEST_F(DumpsysTest, ListAllServicesWithClients) {
    ExpectListServices({"Locksmith", "Valet"});
    ExpectCheckService("Locksmith");
    ExpectCheckService("Valet");

    CallMain({"--clients"});

    AssertRunningServices({"Locksmith", "Valet"});

    const std::string format("(.|\n)*((Client PIDs are not available for local binders.)(.|\n)*){2}");
    AssertOutputFormat(format);
}

// Tests 'dumpsys --clients service_name'
TEST_F(DumpsysTest, ListServiceWithClients) {
    ExpectCheckService("Locksmith");

    CallMain({"--clients", "Locksmith"});

    const std::string format("Client PIDs are not available for local binders.\n");
    AssertOutputFormat(format);
}
// Tests 'dumpsys --thread --stability'
TEST_F(DumpsysTest, ListAllServicesWithMultipleOptions) {
    ExpectListServices({"Locksmith", "Valet"});
    ExpectCheckService("Locksmith");
    ExpectCheckService("Valet");

    CallMain({"--pid", "--stability"});
    AssertRunningServices({"Locksmith", "Valet"});

    AssertOutputContains(std::to_string(getpid()));
    AssertOutputContains("stability");
}

// Tests 'dumpsys --pid --stability service_name'
TEST_F(DumpsysTest, ListServiceWithMultipleOptions) {
    ExpectCheckService("Locksmith");
    CallMain({"--pid", "--stability", "Locksmith"});

    AssertOutputContains(std::to_string(getpid()));
    AssertOutputContains("stability");
}

TEST_F(DumpsysTest, GetBytesWritten) {
    const char* serviceName = "service2";
    const char* dumpContents = "dump1";
    ExpectDump(serviceName, dumpContents);

    String16 service(serviceName);
    Vector<String16> args;
    std::chrono::duration<double> elapsedDuration;
    size_t bytesWritten;

    CallSingleService(service, args, IServiceManager::DUMP_FLAG_PRIORITY_ALL,
                      /* as_proto = */ false, elapsedDuration, bytesWritten);

    AssertOutput(dumpContents);
    EXPECT_THAT(bytesWritten, Eq(strlen(dumpContents)));
}

TEST_F(DumpsysTest, WriteDumpWithoutThreadStart) {
    std::chrono::duration<double> elapsedDuration;
    size_t bytesWritten;
    status_t status =
        dump_.writeDump(STDOUT_FILENO, String16("service"), std::chrono::milliseconds(500),
                        /* as_proto = */ false, elapsedDuration, bytesWritten);
    EXPECT_THAT(status, Eq(INVALID_OPERATION));
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);

    // start a binder thread pool for testing --thread option
    android::ProcessState::self()->setThreadPoolMaxThreadCount(8);
    ProcessState::self()->startThreadPool();

    return RUN_ALL_TESTS();
}
