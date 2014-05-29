// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "sandbox/mac/bootstrap_sandbox.h"

#include <CoreFoundation/CoreFoundation.h>
#import <Foundation/Foundation.h>
#include <mach/mach.h>
#include <servers/bootstrap.h>

#include "base/logging.h"
#include "base/mac/mac_util.h"
#include "base/mac/scoped_nsobject.h"
#include "base/mac/scoped_mach_port.h"
#include "base/process/kill.h"
#include "base/test/multiprocess_test.h"
#include "base/test/test_timeouts.h"
#import "testing/gtest_mac.h"
#include "testing/multiprocess_func_list.h"

NSString* const kTestNotification = @"org.chromium.bootstrap_sandbox_test";

@interface DistributedNotificationObserver : NSObject {
 @private
  int receivedCount_;
  base::scoped_nsobject<NSString> object_;
}
- (int)receivedCount;
- (NSString*)object;
- (void)waitForNotification;
@end

@implementation DistributedNotificationObserver
- (id)init {
  if ((self = [super init])) {
    [[NSDistributedNotificationCenter defaultCenter]
        addObserver:self
           selector:@selector(observeNotification:)
               name:kTestNotification
             object:nil];
  }
  return self;
}

- (void)dealloc {
  [[NSDistributedNotificationCenter defaultCenter]
      removeObserver:self
                name:kTestNotification
              object:nil];
  [super dealloc];
}

- (int)receivedCount {
  return receivedCount_;
}

- (NSString*)object {
  return object_.get();
}

- (void)waitForNotification {
  object_.reset();
  CFRunLoopRunInMode(kCFRunLoopDefaultMode,
      TestTimeouts::action_timeout().InSeconds(), false);
}

- (void)observeNotification:(NSNotification*)notification {
  ++receivedCount_;
  object_.reset([[notification object] copy]);
  CFRunLoopStop(CFRunLoopGetCurrent());
}
@end

////////////////////////////////////////////////////////////////////////////////

namespace sandbox {

class BootstrapSandboxTest : public base::MultiProcessTest {
 public:
  virtual void SetUp() OVERRIDE {
    base::MultiProcessTest::SetUp();

    sandbox_ = BootstrapSandbox::Create();
    ASSERT_TRUE(sandbox_.get());
  }

  BootstrapSandboxPolicy BaselinePolicy() {
    BootstrapSandboxPolicy policy;
    if (base::mac::IsOSSnowLeopard())
      policy["com.apple.SecurityServer"] = Rule(POLICY_ALLOW);
    return policy;
  }

  void RunChildWithPolicy(int policy_id,
                          const char* child_name,
                          base::ProcessHandle* out_pid) {
    sandbox_->PrepareToForkWithPolicy(policy_id);
    base::ProcessHandle pid = SpawnChild(child_name);
    ASSERT_GT(pid, 0);
    sandbox_->FinishedFork(pid);
    int code = 0;
    EXPECT_TRUE(base::WaitForExitCode(pid, &code));
    EXPECT_EQ(0, code);
    if (out_pid)
      *out_pid = pid;
  }

 protected:
  scoped_ptr<BootstrapSandbox> sandbox_;
};

const char kNotificationTestMain[] = "PostNotification";

// Run the test without the sandbox.
TEST_F(BootstrapSandboxTest, DistributedNotifications_Unsandboxed) {
  base::scoped_nsobject<DistributedNotificationObserver> observer(
      [[DistributedNotificationObserver alloc] init]);

  base::ProcessHandle pid = SpawnChild(kNotificationTestMain);
  ASSERT_GT(pid, 0);
  int code = 0;
  EXPECT_TRUE(base::WaitForExitCode(pid, &code));
  EXPECT_EQ(0, code);

  [observer waitForNotification];
  EXPECT_EQ(1, [observer receivedCount]);
  EXPECT_EQ(pid, [[observer object] intValue]);
}

// Run the test with the sandbox enabled without notifications on the policy
// whitelist.
TEST_F(BootstrapSandboxTest, DistributedNotifications_SandboxDeny) {
  base::scoped_nsobject<DistributedNotificationObserver> observer(
      [[DistributedNotificationObserver alloc] init]);

  sandbox_->RegisterSandboxPolicy(1, BaselinePolicy());
  RunChildWithPolicy(1, kNotificationTestMain, NULL);

  [observer waitForNotification];
  EXPECT_EQ(0, [observer receivedCount]);
  EXPECT_EQ(nil, [observer object]);
}

// Run the test with notifications permitted.
TEST_F(BootstrapSandboxTest, DistributedNotifications_SandboxAllow) {
  base::scoped_nsobject<DistributedNotificationObserver> observer(
      [[DistributedNotificationObserver alloc] init]);

  BootstrapSandboxPolicy policy(BaselinePolicy());
  // 10.9:
  policy["com.apple.distributed_notifications@Uv3"] = Rule(POLICY_ALLOW);
  policy["com.apple.distributed_notifications@1v3"] = Rule(POLICY_ALLOW);
  // 10.6:
  policy["com.apple.system.notification_center"] = Rule(POLICY_ALLOW);
  policy["com.apple.distributed_notifications.2"] = Rule(POLICY_ALLOW);
  sandbox_->RegisterSandboxPolicy(2, policy);

  base::ProcessHandle pid;
  RunChildWithPolicy(2, kNotificationTestMain, &pid);

  [observer waitForNotification];
  EXPECT_EQ(1, [observer receivedCount]);
  EXPECT_EQ(pid, [[observer object] intValue]);
}

MULTIPROCESS_TEST_MAIN(PostNotification) {
  [[NSDistributedNotificationCenter defaultCenter]
      postNotificationName:kTestNotification
                    object:[NSString stringWithFormat:@"%d", getpid()]];
  return 0;
}

const char kTestServer[] = "org.chromium.test_bootstrap_server";

TEST_F(BootstrapSandboxTest, PolicyDenyError) {
  BootstrapSandboxPolicy policy(BaselinePolicy());
  policy[kTestServer] = Rule(POLICY_DENY_ERROR);
  sandbox_->RegisterSandboxPolicy(1, policy);

  RunChildWithPolicy(1, "PolicyDenyError", NULL);
}

MULTIPROCESS_TEST_MAIN(PolicyDenyError) {
  mach_port_t port = MACH_PORT_NULL;
  kern_return_t kr = bootstrap_look_up(bootstrap_port, kTestServer,
      &port);
  CHECK_EQ(BOOTSTRAP_UNKNOWN_SERVICE, kr);
  CHECK(port == MACH_PORT_NULL);

  kr = bootstrap_look_up(bootstrap_port, "org.chromium.some_other_server",
      &port);
  CHECK_EQ(BOOTSTRAP_UNKNOWN_SERVICE, kr);
  CHECK(port == MACH_PORT_NULL);

  return 0;
}

TEST_F(BootstrapSandboxTest, PolicyDenyDummyPort) {
  BootstrapSandboxPolicy policy(BaselinePolicy());
  policy[kTestServer] = Rule(POLICY_DENY_DUMMY_PORT);
  sandbox_->RegisterSandboxPolicy(1, policy);

  RunChildWithPolicy(1, "PolicyDenyDummyPort", NULL);
}

MULTIPROCESS_TEST_MAIN(PolicyDenyDummyPort) {
  mach_port_t port = MACH_PORT_NULL;
  kern_return_t kr = bootstrap_look_up(bootstrap_port, kTestServer,
      &port);
  CHECK_EQ(KERN_SUCCESS, kr);
  CHECK(port != MACH_PORT_NULL);
  return 0;
}

struct SubstitutePortAckSend {
  mach_msg_header_t header;
  char buf[32];
};

struct SubstitutePortAckRecv : public SubstitutePortAckSend {
  mach_msg_trailer_t trailer;
};

const char kSubstituteAck[] = "Hello, this is doge!";

TEST_F(BootstrapSandboxTest, PolicySubstitutePort) {
  mach_port_t port;
  ASSERT_EQ(KERN_SUCCESS, mach_port_allocate(mach_task_self(),
      MACH_PORT_RIGHT_RECEIVE, &port));
  base::mac::ScopedMachReceiveRight scoped_port(port);

  BootstrapSandboxPolicy policy(BaselinePolicy());
  policy[kTestServer] = Rule(port);
  sandbox_->RegisterSandboxPolicy(1, policy);

  RunChildWithPolicy(1, "PolicySubstitutePort", NULL);

  struct SubstitutePortAckRecv msg;
  bzero(&msg, sizeof(msg));
  msg.header.msgh_size = sizeof(msg);
  msg.header.msgh_local_port = port;
  kern_return_t kr = mach_msg(&msg.header, MACH_RCV_MSG, 0,
      msg.header.msgh_size, port,
      TestTimeouts::tiny_timeout().InMilliseconds(), MACH_PORT_NULL);
  EXPECT_EQ(KERN_SUCCESS, kr);

  EXPECT_EQ(0, strncmp(kSubstituteAck, msg.buf, sizeof(msg.buf)));
}

MULTIPROCESS_TEST_MAIN(PolicySubstitutePort) {
  mach_port_t port = MACH_PORT_NULL;
  kern_return_t kr = bootstrap_look_up(bootstrap_port, kTestServer, &port);
  CHECK_EQ(KERN_SUCCESS, kr);
  CHECK(port != MACH_PORT_NULL);

  struct SubstitutePortAckSend msg;
  bzero(&msg, sizeof(msg));
  msg.header.msgh_size = sizeof(msg);
  msg.header.msgh_remote_port = port;
  msg.header.msgh_bits = MACH_MSGH_BITS_REMOTE(MACH_MSG_TYPE_MOVE_SEND);
  strncpy(msg.buf, kSubstituteAck, sizeof(msg.buf));

  CHECK_EQ(KERN_SUCCESS, mach_msg_send(&msg.header));

  return 0;
}

TEST_F(BootstrapSandboxTest, ForwardMessageInProcess) {
  mach_port_t task = mach_task_self();

  mach_port_t port;
  ASSERT_EQ(KERN_SUCCESS, mach_port_allocate(task, MACH_PORT_RIGHT_RECEIVE,
      &port));
  base::mac::ScopedMachReceiveRight scoped_port_recv(port);

  mach_port_urefs_t send_rights = 0;
  ASSERT_EQ(KERN_SUCCESS, mach_port_get_refs(task, port, MACH_PORT_RIGHT_SEND,
      &send_rights));
  EXPECT_EQ(0u, send_rights);

  ASSERT_EQ(KERN_SUCCESS, mach_port_insert_right(task, port, port,
      MACH_MSG_TYPE_MAKE_SEND));
  base::mac::ScopedMachSendRight scoped_port_send(port);

  send_rights = 0;
  ASSERT_EQ(KERN_SUCCESS, mach_port_get_refs(task, port, MACH_PORT_RIGHT_SEND,
      &send_rights));
  EXPECT_EQ(1u, send_rights);

  mach_port_t bp;
  ASSERT_EQ(KERN_SUCCESS, task_get_bootstrap_port(task, &bp));
  base::mac::ScopedMachSendRight scoped_bp(bp);

  char service_name[] = "org.chromium.sandbox.test.ForwardMessageInProcess";
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
  kern_return_t kr = bootstrap_register(bp, service_name, port);
#pragma GCC diagnostic pop
  EXPECT_EQ(KERN_SUCCESS, kr);

  send_rights = 0;
  ASSERT_EQ(KERN_SUCCESS, mach_port_get_refs(task, port, MACH_PORT_RIGHT_SEND,
      &send_rights));
  EXPECT_EQ(1u, send_rights);

  mach_port_t service_port;
  EXPECT_EQ(KERN_SUCCESS, bootstrap_look_up(bp, service_name, &service_port));
  base::mac::ScopedMachSendRight scoped_service_port(service_port);

  send_rights = 0;
  ASSERT_EQ(KERN_SUCCESS, mach_port_get_refs(task, port, MACH_PORT_RIGHT_SEND,
      &send_rights));
  EXPECT_EQ(2u, send_rights);
}

}  // namespace sandbox
