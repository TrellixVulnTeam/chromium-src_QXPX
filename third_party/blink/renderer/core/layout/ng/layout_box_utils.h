// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THIRD_PARTY_BLINK_RENDERER_CORE_LAYOUT_NG_LAYOUT_BOX_UTILS_H_
#define THIRD_PARTY_BLINK_RENDERER_CORE_LAYOUT_NG_LAYOUT_BOX_UTILS_H_

#include "third_party/blink/renderer/platform/geometry/layout_unit.h"

namespace blink {

class LayoutBox;
class LayoutBlock;
struct NGStaticPosition;

// This static class should be used for querying information from a |LayoutBox|.
class LayoutBoxUtils {
  STATIC_ONLY(LayoutBoxUtils);

 public:
  // Returns the available logical width/height for |box| accounting for:
  //  - Orthogonal writing modes.
  //  - Any containing block override sizes set.
  static LayoutUnit AvailableLogicalWidth(const LayoutBox& box,
                                          const LayoutBlock* cb);
  static LayoutUnit AvailableLogicalHeight(const LayoutBox& box,
                                           const LayoutBlock* cb);

  // Produces an |NGStaticPosition| for |box| from the layout-tree.
  // |container_builder| is needed as not all information from current NG
  // layout is copied to the layout-tree yet.
  static NGStaticPosition ComputeStaticPositionFromLegacy(const LayoutBox& box);
};

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_CORE_LAYOUT_NG_LAYOUT_BOX_UTILS_H_
