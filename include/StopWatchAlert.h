// SPDX-License-Identifier: MIT
// Copyright (c) 2026 imliubo

#pragma once

#include <stddef.h>
#include <stdint.h>

namespace codex_micro {

// Status colors sent by ChatGPT Desktop in `v.oai.thstatus`.
constexpr uint32_t kCodexColorAwaitingApproval = 0xFF6D00;
constexpr uint32_t kCodexColorUnread = 0x00FF4C;

enum class StopWatchAlert : uint8_t {
  None,
  NeedsApproval,
  Completed,
};

inline StopWatchAlert stopWatchAlertForColor(uint32_t color) {
  switch (color & 0xFFFFFFu) {
    case kCodexColorAwaitingApproval:
      return StopWatchAlert::NeedsApproval;
    case kCodexColorUnread:
      return StopWatchAlert::Completed;
    default:
      return StopWatchAlert::None;
  }
}

// Reports an alert only when an Agent newly enters an alert color, so repeated
// status refreshes of an unchanged slot stay silent. Approval outranks
// completion when both appear in the same update.
template <typename ThreadCollection>
StopWatchAlert stopWatchAlertForChange(const ThreadCollection& before,
                                       const ThreadCollection& after) {
  StopWatchAlert alert = StopWatchAlert::None;
  for (size_t index = 0; index < after.size(); ++index) {
    const StopWatchAlert current = stopWatchAlertForColor(after[index].color);
    if (current == StopWatchAlert::None) {
      continue;
    }
    if (index < before.size() &&
        stopWatchAlertForColor(before[index].color) == current) {
      continue;
    }
    if (current == StopWatchAlert::NeedsApproval) {
      return current;
    }
    alert = current;
  }
  return alert;
}

// Paces a fixed number of beeps without blocking the BLE and HID loop.
class StopWatchBeepSequence {
 public:
  explicit StopWatchBeepSequence(uint32_t intervalMs = 200)
      : intervalMs_(intervalMs == 0 ? 1 : intervalMs) {}

  bool start(uint32_t now, uint8_t count) {
    remaining_ = count;
    lastBeepMs_ = now;
    return emitNext();
  }

  // Unsigned subtraction keeps pacing correct across millis() rollover.
  bool update(uint32_t now) {
    if (remaining_ == 0 || now - lastBeepMs_ < intervalMs_) {
      return false;
    }
    lastBeepMs_ = now;
    return emitNext();
  }

  void stop() { remaining_ = 0; }
  bool active() const { return remaining_ != 0; }

 private:
  bool emitNext() {
    if (remaining_ == 0) {
      return false;
    }
    --remaining_;
    return true;
  }

  uint32_t intervalMs_;
  uint32_t lastBeepMs_ = 0;
  uint8_t remaining_ = 0;
};

}  // namespace codex_micro
