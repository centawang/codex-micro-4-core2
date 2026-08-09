// SPDX-License-Identifier: MIT
// Copyright (c) 2026 imliubo

#pragma once

#include <stdint.h>

namespace codex_micro {

enum class StickS3PowerAction : uint8_t {
  None,
  DimDisplay,
  RestoreDisplay,
  PowerOff,
};

// Tracks inactivity without depending on Arduino or M5Unified. Unsigned
// timestamp subtraction keeps both timeouts valid when millis() wraps around.
class StickS3PowerController {
 public:
  explicit StickS3PowerController(uint32_t dimAfterMs = 60U * 1000U,
                                  uint32_t powerOffAfterMs = 30U * 60U * 1000U)
      : dimAfterMs_(dimAfterMs), powerOffAfterMs_(powerOffAfterMs) {}

  void begin(uint32_t now) {
    lastActivityMs_ = now;
    dimmed_ = false;
    powerOffRequested_ = false;
    initialized_ = true;
  }

  StickS3PowerAction onActivity(uint32_t now) {
    if (!initialized_) {
      begin(now);
      return StickS3PowerAction::None;
    }

    lastActivityMs_ = now;
    powerOffRequested_ = false;
    if (dimmed_) {
      dimmed_ = false;
      return StickS3PowerAction::RestoreDisplay;
    }
    return StickS3PowerAction::None;
  }

  StickS3PowerAction update(uint32_t now) {
    if (!initialized_) {
      begin(now);
      return StickS3PowerAction::None;
    }

    const uint32_t inactiveMs = now - lastActivityMs_;
    if (!powerOffRequested_ && inactiveMs >= powerOffAfterMs_) {
      powerOffRequested_ = true;
      return StickS3PowerAction::PowerOff;
    }
    if (!dimmed_ && inactiveMs >= dimAfterMs_) {
      dimmed_ = true;
      return StickS3PowerAction::DimDisplay;
    }
    return StickS3PowerAction::None;
  }

 private:
  uint32_t dimAfterMs_;
  uint32_t powerOffAfterMs_;
  uint32_t lastActivityMs_ = 0;
  bool dimmed_ = false;
  bool powerOffRequested_ = false;
  bool initialized_ = false;
};

}  // namespace codex_micro
