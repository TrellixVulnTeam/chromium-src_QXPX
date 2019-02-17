// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/platform/scheduler/worker/worker_thread.h"

#include "base/bind.h"
#include "base/location.h"
#include "base/macros.h"
#include "base/synchronization/waitable_event.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/blink/renderer/platform/cross_thread_functional.h"
#include "third_party/blink/renderer/platform/scheduler/public/post_cross_thread_task.h"
#include "third_party/blink/renderer/platform/scheduler/worker/worker_thread_scheduler.h"

using testing::_;
using testing::AnyOf;
using testing::ElementsAre;
using testing::Invoke;

namespace blink {
namespace scheduler {
namespace worker_thread_unittest {

class MockTask {
 public:
  MOCK_METHOD0(Run, void());
};

class MockIdleTask {
 public:
  MOCK_METHOD1(Run, void(double deadline));
};

class TestObserver : public Thread::TaskObserver {
 public:
  explicit TestObserver(std::string* calls) : calls_(calls) {}

  ~TestObserver() override = default;

  void WillProcessTask(const base::PendingTask&) override {
    calls_->append(" willProcessTask");
  }

  void DidProcessTask(const base::PendingTask&) override {
    calls_->append(" didProcessTask");
  }

 private:
  std::string* calls_;  // NOT OWNED
};

void RunTestTask(std::string* calls) {
  calls->append(" run");
}

void AddTaskObserver(Thread* thread, TestObserver* observer) {
  thread->AddTaskObserver(observer);
}

void RemoveTaskObserver(Thread* thread, TestObserver* observer) {
  thread->RemoveTaskObserver(observer);
}

void ShutdownOnThread(Thread* thread) {
  thread->Scheduler()->Shutdown();
}

class WorkerThreadTest : public testing::Test {
 public:
  WorkerThreadTest() = default;

  ~WorkerThreadTest() override = default;

  void SetUp() override {
    thread_ =
        Thread::CreateThread(ThreadCreationParams(WebThreadType::kTestThread));
  }

  void RunOnWorkerThread(const base::Location& from_here,
                         base::OnceClosure task) {
    base::WaitableEvent completion(
        base::WaitableEvent::ResetPolicy::AUTOMATIC,
        base::WaitableEvent::InitialState::NOT_SIGNALED);
    thread_->GetTaskRunner()->PostTask(
        from_here,
        base::BindOnce(&WorkerThreadTest::RunOnWorkerThreadTask,
                       base::Unretained(this), std::move(task), &completion));
    completion.Wait();
  }

 protected:
  void RunOnWorkerThreadTask(base::OnceClosure task,
                             base::WaitableEvent* completion) {
    std::move(task).Run();
    completion->Signal();
  }

  std::unique_ptr<Thread> thread_;

  DISALLOW_COPY_AND_ASSIGN(WorkerThreadTest);
};

TEST_F(WorkerThreadTest, TestDefaultTask) {
  MockTask task;
  base::WaitableEvent completion(
      base::WaitableEvent::ResetPolicy::AUTOMATIC,
      base::WaitableEvent::InitialState::NOT_SIGNALED);

  EXPECT_CALL(task, Run());
  ON_CALL(task, Run()).WillByDefault(Invoke([&completion]() {
    completion.Signal();
  }));

  PostCrossThreadTask(
      *thread_->GetTaskRunner(), FROM_HERE,
      CrossThreadBind(&MockTask::Run, WTF::CrossThreadUnretained(&task)));
  completion.Wait();
}

TEST_F(WorkerThreadTest, TestTaskExecutedBeforeThreadDeletion) {
  MockTask task;
  base::WaitableEvent completion(
      base::WaitableEvent::ResetPolicy::AUTOMATIC,
      base::WaitableEvent::InitialState::NOT_SIGNALED);

  EXPECT_CALL(task, Run());
  ON_CALL(task, Run()).WillByDefault(Invoke([&completion]() {
    completion.Signal();
  }));

  PostCrossThreadTask(
      *thread_->GetTaskRunner(), FROM_HERE,
      CrossThreadBind(&MockTask::Run, WTF::CrossThreadUnretained(&task)));
  thread_.reset();
}

TEST_F(WorkerThreadTest, TestTaskObserver) {
  std::string calls;
  TestObserver observer(&calls);

  RunOnWorkerThread(FROM_HERE,
                    base::BindOnce(&AddTaskObserver, thread_.get(), &observer));
  PostCrossThreadTask(
      *thread_->GetTaskRunner(), FROM_HERE,
      CrossThreadBind(&RunTestTask, WTF::CrossThreadUnretained(&calls)));
  RunOnWorkerThread(
      FROM_HERE, base::BindOnce(&RemoveTaskObserver, thread_.get(), &observer));

  // We need to be careful what we test here.  We want to make sure the
  // observers are un in the expected order before and after the task.
  // Sometimes we get an internal scheduler task running before or after
  // TestTask as well. This is not a bug, and we need to make sure the test
  // doesn't fail when that happens.
  EXPECT_THAT(calls, testing::HasSubstr("willProcessTask run didProcessTask"));
}

TEST_F(WorkerThreadTest, TestShutdown) {
  MockTask task;
  MockTask delayed_task;

  EXPECT_CALL(task, Run()).Times(0);
  EXPECT_CALL(delayed_task, Run()).Times(0);

  RunOnWorkerThread(FROM_HERE,
                    base::BindOnce(&ShutdownOnThread, thread_.get()));
  PostCrossThreadTask(
      *thread_->GetTaskRunner(), FROM_HERE,
      CrossThreadBind(&MockTask::Run, WTF::CrossThreadUnretained(&task)));
  PostDelayedCrossThreadTask(
      *thread_->GetTaskRunner(), FROM_HERE,
      CrossThreadBind(&MockTask::Run,
                      WTF::CrossThreadUnretained(&delayed_task)),
      TimeDelta::FromMilliseconds(50));
  thread_.reset();
}

}  // namespace worker_thread_unittest
}  // namespace scheduler
}  // namespace blink