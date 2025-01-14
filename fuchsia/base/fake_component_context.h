// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FUCHSIA_BASE_FAKE_COMPONENT_CONTEXT_H_
#define FUCHSIA_BASE_FAKE_COMPONENT_CONTEXT_H_

#include <fuchsia/base/agent_impl.h>
#include <fuchsia/modular/cpp/fidl_test_base.h>
#include <memory>
#include <string>
#include <utility>

namespace cr_fuchsia {

// Used to test interactions with an Agent in unit-tests for a component.
// |create_component_state_callback| can be used with a test-specific
// ComponentStateBase to serve fake services to the component.
// |service_directory| specifies the directory into which the ComponentContext
// should be published, alongside any other services the test wishes to provide
// to the component's default service namespace. |component_url| specifies the
// component identity that should be reported to the Agent
class FakeComponentContext
    : public fuchsia::modular::testing::ComponentContext_TestBase {
 public:
  explicit FakeComponentContext(
      AgentImpl::CreateComponentStateCallback create_component_state_callback,
      base::fuchsia::ServiceDirectory* service_directory,
      std::string component_url);
  ~FakeComponentContext() override;

  // fuchsia::modular::ComponentContext implementation.
  void ConnectToAgent(
      std::string agent_url,
      fidl::InterfaceRequest<::fuchsia::sys::ServiceProvider> services,
      fidl::InterfaceRequest<fuchsia::modular::AgentController> controller)
      override;

 private:
  AgentImpl agent_impl_;
  const std::string component_url_;

  DISALLOW_COPY_AND_ASSIGN(FakeComponentContext);
};

}  // namespace cr_fuchsia
#endif  // FUCHSIA_BASE_FAKE_COMPONENT_CONTEXT_H_
