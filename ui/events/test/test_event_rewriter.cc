// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/events/test/test_event_rewriter.h"

#include "ui/events/event.h"

namespace ui {
namespace test {

TestEventRewriter::TestEventRewriter() = default;

TestEventRewriter::~TestEventRewriter() = default;

ui::EventDispatchDetails TestEventRewriter::RewriteEvent(
    const ui::Event& event,
    const Continuation continuation) {
  ++events_seen_;
  return SendEvent(continuation, &event);
}

}  // namespace test
}  // namespace ui