// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THIRD_PARTY_BLINK_RENDERER_CONTROLLER_CRASH_MEMORY_METRICS_REPORTER_IMPL_H_
#define THIRD_PARTY_BLINK_RENDERER_CONTROLLER_CRASH_MEMORY_METRICS_REPORTER_IMPL_H_

#include "base/files/scoped_file.h"
#include "mojo/public/cpp/bindings/binding.h"
#include "third_party/blink/public/common/oom_intervention/oom_intervention_types.h"
#include "third_party/blink/public/mojom/crash/crash_memory_metrics_reporter.mojom-blink.h"
#include "third_party/blink/renderer/controller/controller_export.h"
#include "third_party/blink/renderer/controller/memory_usage_monitor.h"

namespace blink {

// Writes data about renderer into shared memory that will be read by browser.
class CONTROLLER_EXPORT CrashMemoryMetricsReporterImpl
    : public mojom::blink::CrashMemoryMetricsReporter,
      public MemoryUsageMonitor::Observer {
 public:
  static CrashMemoryMetricsReporterImpl& Instance();
  static void Bind(mojom::blink::CrashMemoryMetricsReporterRequest);
  static OomInterventionMetrics MemoryUsageToMetrics(MemoryUsage);

  ~CrashMemoryMetricsReporterImpl() override;

  // mojom::CrashMemoryMetricsReporter implementations:
  void SetSharedMemory(
      base::UnsafeSharedMemoryRegion shared_metrics_buffer) override;

  // MemoryUsageMonitor::Observer:
  void OnMemoryPing(MemoryUsage) override;

  // This method tracks when an allocation failure occurs. It should be hooked
  // into all platform allocation failure handlers in a process such as
  // base::TerminateBecauseOutOfMemory() and OOM_CRASH() in Partition Alloc.
  // TODO(yuzus): Now only called from OOM_CRASH(). Call this from malloc/new
  // failures and base::TerminateBecauseOutOfMemory(), too.
  static void OnOOMCallback();

  // This function needs to be called after ResetFileDescriptors.
  OomInterventionMetrics GetCurrentMemoryMetrics();

 protected:
  CrashMemoryMetricsReporterImpl();

 private:
  FRIEND_TEST_ALL_PREFIXES(OomInterventionImplTest, CalculateProcessFootprint);

  void WriteIntoSharedMemory(const OomInterventionMetrics& metrics);

  base::WritableSharedMemoryMapping shared_metrics_mapping_;
  mojo::Binding<mojom::blink::CrashMemoryMetricsReporter> binding_;
};
}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_CONTROLLER_CRASH_MEMORY_METRICS_REPORTER_IMPL_H_
