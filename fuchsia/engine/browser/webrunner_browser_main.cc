// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fuchsia/engine/browser/webrunner_browser_main.h"

#include <memory>

#include "base/logging.h"
#include "build/build_config.h"
#include "content/public/browser/browser_main_runner.h"

int WebRunnerBrowserMain(const content::MainFunctionParams& parameters) {
  std::unique_ptr<content::BrowserMainRunner> main_runner =
      content::BrowserMainRunner::Create();
  int exit_code = main_runner->Initialize(parameters);
  if (exit_code >= 0)
    return exit_code;

  exit_code = main_runner->Run();

  main_runner->Shutdown();

  return exit_code;
}
