// SPDX-License-Identifier: MIT
// Copyright (c) 2026 imliubo

#pragma once

#include <stdint.h>

namespace codex_micro {

inline int8_t stopWatchSwipePageDelta(int32_t distanceX, int32_t distanceY,
                                      int32_t minimumDistance = 70) {
  const int32_t absoluteX = distanceX < 0 ? -distanceX : distanceX;
  const int32_t absoluteY = distanceY < 0 ? -distanceY : distanceY;
  if (absoluteX < minimumDistance || absoluteX <= absoluteY) {
    return 0;
  }
  return distanceX < 0 ? 1 : -1;
}

}  // namespace codex_micro