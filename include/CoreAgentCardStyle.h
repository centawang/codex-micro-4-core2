// SPDX-License-Identifier: MIT
// Copyright (c) 2026 imliubo

#pragma once

#include <stdint.h>

namespace codex_micro {

inline uint16_t dimRgb565(uint16_t color) {
  const uint16_t red = ((color >> 11) & 0x1F) * 3 / 4;
  const uint16_t green = ((color >> 5) & 0x3F) * 3 / 4;
  const uint16_t blue = (color & 0x1F) * 3 / 4;
  return static_cast<uint16_t>((red << 11) | (green << 5) | blue);
}

inline uint16_t agentCardFill(uint16_t statusColor, bool assigned, bool pressed,
                              uint16_t inactiveFill) {
  const uint16_t fill = assigned ? statusColor : inactiveFill;
  return pressed ? dimRgb565(fill) : fill;
}

inline uint16_t agentCardTextColor(uint16_t fill, uint16_t lightText,
                                   uint16_t darkText) {
  const uint32_t red = ((fill >> 11) & 0x1F) * 255 / 31;
  const uint32_t green = ((fill >> 5) & 0x3F) * 255 / 63;
  const uint32_t blue = (fill & 0x1F) * 255 / 31;
  const uint32_t luma = red * 299 + green * 587 + blue * 114;
  return luma >= 150000 ? darkText : lightText;
}

}  // namespace codex_micro
