// SPDX-License-Identifier: MIT
// Copyright (c) 2026 imliubo

#pragma once

#include <stdint.h>

namespace codex_micro {

enum class StopWatchButtonAction : uint8_t {
  None,
  ButtonASingle,
  ButtonADouble,
  ButtonBSingle,
  ButtonBDouble,
};

class StopWatchButtonController {
 public:
  explicit StopWatchButtonController(uint32_t doubleClickMs = 350)
      : doubleClickMs_(doubleClickMs) {}

  StopWatchButtonAction onButtonAReleased(uint32_t now) {
    return onReleased(now, buttonA_, StopWatchButtonAction::ButtonASingle,
                      StopWatchButtonAction::ButtonADouble);
  }

  StopWatchButtonAction onButtonBReleased(uint32_t now) {
    return onReleased(now, buttonB_, StopWatchButtonAction::ButtonBSingle,
                      StopWatchButtonAction::ButtonBDouble);
  }

  StopWatchButtonAction pollButtonA(uint32_t now, bool buttonPressed) {
    return poll(now, buttonPressed, buttonA_,
                StopWatchButtonAction::ButtonASingle);
  }

  StopWatchButtonAction pollButtonB(uint32_t now, bool buttonPressed) {
    return poll(now, buttonPressed, buttonB_,
                StopWatchButtonAction::ButtonBSingle);
  }

 private:
  struct ButtonState {
    bool pending = false;
    uint32_t releaseMs = 0;
  };

  static uint32_t elapsed(uint32_t now, uint32_t then) { return now - then; }

  StopWatchButtonAction onReleased(uint32_t now, ButtonState& state,
                                   StopWatchButtonAction singleAction,
                                   StopWatchButtonAction doubleAction) {
    if (!state.pending) {
      state.pending = true;
      state.releaseMs = now;
      return StopWatchButtonAction::None;
    }

    if (elapsed(now, state.releaseMs) <= doubleClickMs_) {
      state.pending = false;
      return doubleAction;
    }

    state.releaseMs = now;
    return singleAction;
  }

  StopWatchButtonAction poll(uint32_t now, bool buttonPressed,
                             ButtonState& state,
                             StopWatchButtonAction singleAction) {
    if (state.pending && !buttonPressed &&
        elapsed(now, state.releaseMs) > doubleClickMs_) {
      state.pending = false;
      return singleAction;
    }
    return StopWatchButtonAction::None;
  }

  uint32_t doubleClickMs_;
  ButtonState buttonA_;
  ButtonState buttonB_;
};

}  // namespace codex_micro