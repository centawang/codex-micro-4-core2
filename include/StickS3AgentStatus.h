// SPDX-License-Identifier: MIT
// Copyright (c) 2026 imliubo

#pragma once

#include <stddef.h>

namespace codex_micro {

template <typename ThreadCollection>
bool agentStatusesChanged(const ThreadCollection& before,
                          const ThreadCollection& after) {
  if (before.size() != after.size()) {
    return true;
  }

  for (size_t i = 0; i < before.size(); ++i) {
    const auto& previous = before[i];
    const auto& current = after[i];
    if (previous.color != current.color ||
        previous.brightness != current.brightness ||
        previous.effect != current.effect || previous.speed != current.speed) {
      return true;
    }
  }
  return false;
}

}  // namespace codex_micro
