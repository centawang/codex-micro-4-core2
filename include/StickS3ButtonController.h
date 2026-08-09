// SPDX-License-Identifier: MIT
// Copyright (c) 2026 imliubo

#pragma once

#include <stdint.h>

namespace codex_micro {

enum class StickS3ButtonAction : uint8_t {
  None,
  SelectionChanged,
  ActivateSelectedAgent,
  SendEnter,
  SendRightAlt,
};

// Resolves delayed single-clicks without depending on Arduino or M5Unified.
// Unsigned timestamp subtraction intentionally keeps the timers valid when
// millis() wraps around.
class StickS3ButtonController {
 public:
  explicit StickS3ButtonController(uint8_t agentCount = 6,
                                   uint32_t doubleClickMs = 350)
      : agentCount_(agentCount == 0 ? 1 : agentCount),
        doubleClickMs_(doubleClickMs) {}

  uint8_t selectedAgent() const { return selectedAgent_; }

  StickS3ButtonAction onButtonAReleased(uint32_t now) {
    if (!pendingButtonA_) {
      pendingButtonA_ = true;
      buttonAReleaseMs_ = now;
      return StickS3ButtonAction::None;
    }

    if (elapsed(now, buttonAReleaseMs_) <= doubleClickMs_) {
      pendingButtonA_ = false;
      return StickS3ButtonAction::SendRightAlt;
    }

    // The earlier click has expired but was not polled before this release.
    // Emit it now and retain this release as the start of a new click.
    buttonAReleaseMs_ = now;
    return StickS3ButtonAction::SendEnter;
  }

  StickS3ButtonAction onButtonBHold() {
    pendingButtonB_ = false;
    return StickS3ButtonAction::ActivateSelectedAgent;
  }

  StickS3ButtonAction onButtonBReleased(uint32_t now,
                                        bool releasedAfterHold) {
    if (releasedAfterHold) {
      pendingButtonB_ = false;
      return StickS3ButtonAction::None;
    }

    if (!pendingButtonB_) {
      pendingButtonB_ = true;
      buttonBReleaseMs_ = now;
      return StickS3ButtonAction::None;
    }

    if (elapsed(now, buttonBReleaseMs_) <= doubleClickMs_) {
      pendingButtonB_ = false;
      selectedAgent_ = static_cast<uint8_t>(
          (selectedAgent_ + agentCount_ - 1) % agentCount_);
      return StickS3ButtonAction::SelectionChanged;
    }

    // Resolve the expired click and retain this release for the next click.
    selectedAgent_ = static_cast<uint8_t>((selectedAgent_ + 1) % agentCount_);
    buttonBReleaseMs_ = now;
    return StickS3ButtonAction::SelectionChanged;
  }

  StickS3ButtonAction pollButtonA(uint32_t now) {
    if (pendingButtonA_ &&
        elapsed(now, buttonAReleaseMs_) > doubleClickMs_) {
      pendingButtonA_ = false;
      return StickS3ButtonAction::SendEnter;
    }
    return StickS3ButtonAction::None;
  }

  StickS3ButtonAction pollButtonB(uint32_t now, bool buttonPressed) {
    if (pendingButtonB_ && !buttonPressed &&
        elapsed(now, buttonBReleaseMs_) > doubleClickMs_) {
      pendingButtonB_ = false;
      selectedAgent_ =
          static_cast<uint8_t>((selectedAgent_ + 1) % agentCount_);
      return StickS3ButtonAction::SelectionChanged;
    }
    return StickS3ButtonAction::None;
  }

 private:
  static uint32_t elapsed(uint32_t now, uint32_t then) { return now - then; }

  uint8_t agentCount_;
  uint32_t doubleClickMs_;
  uint8_t selectedAgent_ = 0;
  bool pendingButtonA_ = false;
  bool pendingButtonB_ = false;
  uint32_t buttonAReleaseMs_ = 0;
  uint32_t buttonBReleaseMs_ = 0;
};

}  // namespace codex_micro
