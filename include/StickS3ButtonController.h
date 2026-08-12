// SPDX-License-Identifier: MIT
// Copyright (c) 2026 imliubo

#pragma once

#include <stdint.h>

#include "StickS3ButtonConfig.h"

namespace codex_micro {

// Resolves delayed single-clicks without depending on Arduino or M5Unified.
// Unsigned timestamp subtraction intentionally keeps the timers valid when
// millis() wraps around.
class StickS3ButtonController {
 public:
  explicit StickS3ButtonController(uint8_t agentCount = 6,
                                   uint32_t doubleClickMs = 350)
      : agentCount_(agentCount == 0 ? 1 : agentCount),
        doubleClickMs_(doubleClickMs) {
      actions_[slotIndex(StickS3ButtonSlot::ButtonASingle)] =
        makeStickS3ButtonAction(StickS3ButtonActionKind::SendEnter);
      actions_[slotIndex(StickS3ButtonSlot::ButtonADouble)] =
        makeStickS3ButtonAction(StickS3ButtonActionKind::SendRightAlt);
      actions_[slotIndex(StickS3ButtonSlot::ButtonBSingle)] =
        makeStickS3SelectionAction(1);
      actions_[slotIndex(StickS3ButtonSlot::ButtonBDouble)] =
        makeStickS3SelectionAction(-1);
      actions_[slotIndex(StickS3ButtonSlot::ButtonBHold)] =
        makeStickS3ButtonAction(
          StickS3ButtonActionKind::ActivateSelectedAgent);
      }

  uint8_t selectedAgent() const { return selectedAgent_; }

      void setAction(StickS3ButtonSlot slot, StickS3ButtonAction action) {
    const uint8_t index = slotIndex(slot);
    if (index < slotIndex(StickS3ButtonSlot::Count)) {
      actions_[index] = action;
      }
      }

  StickS3ButtonAction onButtonAReleased(uint32_t now) {
    if (!pendingButtonA_) {
      pendingButtonA_ = true;
      buttonAReleaseMs_ = now;
      return StickS3ButtonAction::None;
    }

    if (elapsed(now, buttonAReleaseMs_) <= doubleClickMs_) {
      pendingButtonA_ = false;
      return resolveAction(StickS3ButtonSlot::ButtonADouble);
    }

    // The earlier click has expired but was not polled before this release.
    // Emit it now and retain this release as the start of a new click.
    buttonAReleaseMs_ = now;
    return resolveAction(StickS3ButtonSlot::ButtonASingle);
  }

  StickS3ButtonAction onButtonBHold() {
    pendingButtonB_ = false;
    return resolveAction(StickS3ButtonSlot::ButtonBHold);
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
      return resolveAction(StickS3ButtonSlot::ButtonBDouble);
    }

    // Resolve the expired click and retain this release for the next click.
    buttonBReleaseMs_ = now;
    return resolveAction(StickS3ButtonSlot::ButtonBSingle);
  }

  StickS3ButtonAction pollButtonA(uint32_t now) {
    if (pendingButtonA_ &&
        elapsed(now, buttonAReleaseMs_) > doubleClickMs_) {
      pendingButtonA_ = false;
      return resolveAction(StickS3ButtonSlot::ButtonASingle);
    }
    return StickS3ButtonAction::None;
  }

  StickS3ButtonAction pollButtonB(uint32_t now, bool buttonPressed) {
    if (pendingButtonB_ && !buttonPressed &&
        elapsed(now, buttonBReleaseMs_) > doubleClickMs_) {
      pendingButtonB_ = false;
        return resolveAction(StickS3ButtonSlot::ButtonBSingle);
    }
    return StickS3ButtonAction::None;
  }

 private:
  static constexpr uint8_t slotIndex(StickS3ButtonSlot slot) {
    return static_cast<uint8_t>(slot);
  }

  static uint32_t elapsed(uint32_t now, uint32_t then) { return now - then; }

  StickS3ButtonAction resolveAction(StickS3ButtonSlot slot) {
    const StickS3ButtonAction action = actions_[slotIndex(slot)];
    if (action.kind == StickS3ButtonActionKind::SelectionChanged) {
      if (action.selectionDelta > 0) {
        selectedAgent_ =
            static_cast<uint8_t>((selectedAgent_ + 1) % agentCount_);
      } else if (action.selectionDelta < 0) {
        selectedAgent_ = static_cast<uint8_t>(
            (selectedAgent_ + agentCount_ - 1) % agentCount_);
      }
    }
    return action;
  }

  uint8_t agentCount_;
  uint32_t doubleClickMs_;
  StickS3ButtonAction
      actions_[static_cast<uint8_t>(StickS3ButtonSlot::Count)] = {};
  uint8_t selectedAgent_ = 0;
  bool pendingButtonA_ = false;
  bool pendingButtonB_ = false;
  uint32_t buttonAReleaseMs_ = 0;
  uint32_t buttonBReleaseMs_ = 0;
};

}  // namespace codex_micro
