// SPDX-License-Identifier: MIT
// Copyright (c) 2026 imliubo

#pragma once

#include <stdint.h>

namespace codex_micro {

// Drives a non-blocking full-screen flash. Each flash consists of one visible
// phase followed by one normal-screen phase.
class StickS3ScreenFlash {
 public:
  explicit StickS3ScreenFlash(uint32_t phaseMs = 100,
                              uint8_t flashCount = 3)
      : phaseMs_(phaseMs == 0 ? 1 : phaseMs),
        phaseCount_(static_cast<uint16_t>(flashCount) * 2U) {}

  void start(uint32_t now) {
    if (phaseCount_ == 0) {
      active_ = false;
      visible_ = false;
      return;
    }
    active_ = true;
    visible_ = true;
    phase_ = 0;
    phaseStartedMs_ = now;
  }

  // Returns true only when the caller needs to redraw the display. Advancing
  // at most one phase per call guarantees that every phase is shown even if a
  // loop iteration is delayed. Unsigned subtraction handles millis() rollover.
  bool update(uint32_t now) {
    if (!active_ || now - phaseStartedMs_ < phaseMs_) {
      return false;
    }

    phaseStartedMs_ = now;
    ++phase_;
    const bool wasVisible = visible_;
    if (phase_ >= phaseCount_) {
      active_ = false;
      visible_ = false;
    } else {
      visible_ = (phase_ % 2) == 0;
    }
    return visible_ != wasVisible;
  }

  bool visible() const { return visible_; }
  bool active() const { return active_; }

 private:
  uint32_t phaseMs_;
  uint16_t phaseCount_;
  uint16_t phase_ = 0;
  uint32_t phaseStartedMs_ = 0;
  bool active_ = false;
  bool visible_ = false;
};

}  // namespace codex_micro
