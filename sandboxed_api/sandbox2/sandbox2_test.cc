// Copyright 2019 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "sandboxed_api/sandbox2/sandbox2.h"

#include <fcntl.h>
#include <syscall.h>

#include <csignal>
#include <memory>
#include <string>
#include <thread>  // NOLINT(build/c++11)
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/strings/str_cat.h"
#include "sandboxed_api/config.h"
#include "sandboxed_api/sandbox2/allow_all_syscalls.h"
#include "sandboxed_api/sandbox2/executor.h"
#include "sandboxed_api/sandbox2/policy.h"
#include "sandboxed_api/sandbox2/policybuilder.h"
#include "sandboxed_api/sandbox2/result.h"
#include "sandboxed_api/testing.h"
#include "sandboxed_api/util/status_matchers.h"

namespace sandbox2 {
namespace {

PolicyBuilder CreateDefaultPolicyBuilder(absl::string_view path) {
  PolicyBuilder builder;
  // Don't restrict the syscalls at all.
  builder.DefaultAction(AllowAllSyscalls());
  if constexpr (sapi::sanitizers::IsAny()) {
    builder.AddLibrariesForBinary(path);
  }
  if constexpr (sapi::sanitizers::IsAny()) {
    builder.AddDirectory("/proc");
  }
  return builder;
}

using ::sapi::GetTestSourcePath;
using ::testing::Eq;
using ::testing::HasSubstr;
using ::testing::IsEmpty;
using ::testing::IsTrue;
using ::testing::Lt;

// Test that aborting inside a sandbox with all userspace core dumping
// disabled reports the signal.
TEST(SandboxCoreDumpTest, AbortWithoutCoreDumpReturnsSignaled) {
  SKIP_SANITIZERS_AND_COVERAGE;
  const std::string path = GetTestSourcePath("sandbox2/testcases/abort");
  std::vector<std::string> args = {
      path,
  };
  auto executor = std::make_unique<Executor>(path, args);

  SAPI_ASSERT_OK_AND_ASSIGN(auto policy, CreateDefaultPolicyBuilder(path)
                                             .TryBuild());
  Sandbox2 sandbox(std::move(executor), std::move(policy));
  auto result = sandbox.Run();

  ASSERT_THAT(result.final_status(), Eq(Result::SIGNALED));
  EXPECT_THAT(result.reason_code(), Eq(SIGABRT));
}

// Test that with TSYNC we are able to sandbox when multithreaded and with no
// memory checks. If TSYNC is not supported, then no.
TEST(TsyncTest, TsyncNoMemoryChecks) {
  SKIP_SANITIZERS_AND_COVERAGE;
  const std::string path = GetTestSourcePath("sandbox2/testcases/tsync");

  auto executor =
      std::make_unique<Executor>(path, std::vector<std::string>{path});
  executor->set_enable_sandbox_before_exec(false);

  SAPI_ASSERT_OK_AND_ASSIGN(auto policy,
                            CreateDefaultPolicyBuilder(path).TryBuild());
  Sandbox2 sandbox(std::move(executor), std::move(policy));
  auto result = sandbox.Run();

  // With TSYNC, SandboxMeHere should be able to sandbox when multithreaded.
  ASSERT_EQ(result.final_status(), Result::OK);
  ASSERT_EQ(result.reason_code(), 0);
}

// Tests whether Executor(fd, std::vector<std::string>{path}, envp) constructor
// works as expected.
TEST(ExecutorTest, ExecutorFdConstructor) {
  SKIP_SANITIZERS_AND_COVERAGE;

  const std::string path = GetTestSourcePath("sandbox2/testcases/minimal");
  int fd = open(path.c_str(), O_RDONLY);
  ASSERT_NE(fd, -1);

  std::vector<std::string> args = {absl::StrCat("FD:", fd)};
  std::vector<std::string> envs;
  auto executor = std::make_unique<Executor>(fd, args, envs);

  SAPI_ASSERT_OK_AND_ASSIGN(auto policy,
                            CreateDefaultPolicyBuilder(path).TryBuild());
  Sandbox2 sandbox(std::move(executor), std::move(policy));
  auto result = sandbox.Run();

  ASSERT_EQ(result.final_status(), Result::OK);
}

// Tests that we return the correct state when the sandboxee timed out.
TEST(StackTraceTest, StackTraceOnTimeoutWorks) {
  SKIP_ANDROID;
  const std::string path = GetTestSourcePath("sandbox2/testcases/sleep");

  std::vector<std::string> args = {path};
  std::vector<std::string> envs;
  auto executor = std::make_unique<Executor>(path, args, envs);

  SAPI_ASSERT_OK_AND_ASSIGN(auto policy,
                            PolicyBuilder()
                                // Don't restrict the syscalls at all.
                                .DangerDefaultAllowAll()
                                .TryBuild());
  Sandbox2 sandbox(std::move(executor), std::move(policy));
  ASSERT_TRUE(sandbox.RunAsync());
  sandbox.set_walltime_limit(absl::Seconds(1));
  auto result = sandbox.AwaitResult();
  EXPECT_EQ(result.final_status(), Result::TIMEOUT);
  EXPECT_THAT(result.GetStackTrace(), HasSubstr("sleep"));
}

// Tests that we return the correct state when the sandboxee was killed by an
// external signal. Also make sure that we do not have the stack trace.
TEST(RunAsyncTest, SandboxeeExternalKill) {
  const std::string path = GetTestSourcePath("sandbox2/testcases/sleep");

  std::vector<std::string> args = {path};
  std::vector<std::string> envs;
  auto executor = std::make_unique<Executor>(path, args, envs);

  SAPI_ASSERT_OK_AND_ASSIGN(auto policy,
                            CreateDefaultPolicyBuilder(path).TryBuild());
  Sandbox2 sandbox(std::move(executor), std::move(policy));
  ASSERT_TRUE(sandbox.RunAsync());
  sleep(1);
  sandbox.Kill();
  auto result = sandbox.AwaitResult();
  EXPECT_EQ(result.final_status(), Result::EXTERNAL_KILL);
  EXPECT_THAT(result.GetStackTrace(), IsEmpty());
}

// Tests that we do not collect stack traces if it was disabled (signaled).
TEST(RunAsyncTest, SandboxeeTimeoutDisabledStacktraces) {
  const std::string path = GetTestSourcePath("sandbox2/testcases/sleep");

  std::vector<std::string> args = {path};
  std::vector<std::string> envs;
  auto executor = std::make_unique<Executor>(path, args, envs);

  SAPI_ASSERT_OK_AND_ASSIGN(auto policy, CreateDefaultPolicyBuilder(path)
                                             .CollectStacktracesOnTimeout(false)
                                             .TryBuild());
  Sandbox2 sandbox(std::move(executor), std::move(policy));
  ASSERT_TRUE(sandbox.RunAsync());
  sandbox.set_walltime_limit(absl::Seconds(1));
  auto result = sandbox.AwaitResult();
  EXPECT_EQ(result.final_status(), Result::TIMEOUT);
  EXPECT_THAT(result.GetStackTrace(), IsEmpty());
}

// Tests that we do not collect stack traces if it was disabled (violation).
TEST(RunAsyncTest, SandboxeeViolationDisabledStacktraces) {
  const std::string path = GetTestSourcePath("sandbox2/testcases/sleep");

  std::vector<std::string> args = {path};
  std::vector<std::string> envs;
  auto executor = std::make_unique<Executor>(path, args, envs);

  SAPI_ASSERT_OK_AND_ASSIGN(
      auto policy, PolicyBuilder()
                       // Don't allow anything - Make sure that we'll crash.
                       .CollectStacktracesOnViolation(false)
                       .TryBuild());
  Sandbox2 sandbox(std::move(executor), std::move(policy));
  ASSERT_TRUE(sandbox.RunAsync());
  auto result = sandbox.AwaitResult();
  EXPECT_EQ(result.final_status(), Result::VIOLATION);
  EXPECT_THAT(result.GetStackTrace(), IsEmpty());
}

TEST(RunAsyncTest, SandboxeeNotKilledWhenStartingThreadFinishes) {
  const std::string path = GetTestSourcePath("sandbox2/testcases/minimal");
  std::vector<std::string> args = {path};
  auto executor = std::make_unique<Executor>(path, args);

  SAPI_ASSERT_OK_AND_ASSIGN(auto policy, CreateDefaultPolicyBuilder(path)
                                             .CollectStacktracesOnExit(true)
                                             .TryBuild());
  Sandbox2 sandbox(std::move(executor), std::move(policy));
  std::thread sandbox_start_thread([&sandbox]() { sandbox.RunAsync(); });
  sandbox_start_thread.join();
  Result result = sandbox.AwaitResult();
  EXPECT_EQ(result.final_status(), Result::OK);
}

TEST(StarvationTest, MonitorIsNotStarvedByTheSandboxee) {
  const std::string path = GetTestSourcePath("sandbox2/testcases/starve");

  std::vector<std::string> args = {path};
  std::vector<std::string> envs;
  auto executor = std::make_unique<Executor>(path, args, envs);
  executor->limits()->set_walltime_limit(absl::Seconds(5));

  SAPI_ASSERT_OK_AND_ASSIGN(auto policy,
                            CreateDefaultPolicyBuilder(path).TryBuild());
  Sandbox2 sandbox(std::move(executor), std::move(policy));

  auto start = absl::Now();
  ASSERT_THAT(sandbox.RunAsync(), IsTrue());
  auto result = sandbox.AwaitResult();
  EXPECT_THAT(result.final_status(), Eq(Result::TIMEOUT));

  auto elapsed = absl::Now() - start;
  EXPECT_THAT(elapsed, Lt(absl::Seconds(10)));
}

}  // namespace
}  // namespace sandbox2
