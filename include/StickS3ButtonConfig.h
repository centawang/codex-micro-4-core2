// SPDX-License-Identifier: MIT
// Copyright (c) 2026 imliubo

#pragma once

#include <stdint.h>
#include <stdio.h>
#include <string.h>

namespace codex_micro {

enum class StickS3ButtonSlot : uint8_t {
  ButtonASingle,
  ButtonADouble,
  ButtonBSingle,
  ButtonBDouble,
  ButtonBHold,
  Count,
};

enum class StickS3ButtonActionKind : uint8_t {
  None,
  SelectionChanged,
  ActivateSelectedAgent,
  SendEnter,
  SendRightAlt,
  KeyboardShortcut,
  CodexCommand,
};

struct StickS3ButtonAction {
  static constexpr StickS3ButtonActionKind None =
      StickS3ButtonActionKind::None;
  static constexpr StickS3ButtonActionKind SelectionChanged =
      StickS3ButtonActionKind::SelectionChanged;
  static constexpr StickS3ButtonActionKind ActivateSelectedAgent =
      StickS3ButtonActionKind::ActivateSelectedAgent;
  static constexpr StickS3ButtonActionKind SendEnter =
      StickS3ButtonActionKind::SendEnter;
  static constexpr StickS3ButtonActionKind SendRightAlt =
      StickS3ButtonActionKind::SendRightAlt;
  static constexpr StickS3ButtonActionKind KeyboardShortcut =
      StickS3ButtonActionKind::KeyboardShortcut;
  static constexpr StickS3ButtonActionKind CodexCommand =
      StickS3ButtonActionKind::CodexCommand;

  StickS3ButtonActionKind kind = StickS3ButtonActionKind::None;
  int8_t selectionDelta = 0;
  uint8_t modifier = 0;
  uint8_t key = 0;
  uint8_t commandIndex = 0;

  constexpr StickS3ButtonAction() = default;
  constexpr StickS3ButtonAction(StickS3ButtonActionKind value) : kind(value) {}
};

inline StickS3ButtonAction makeStickS3ButtonAction(
    StickS3ButtonActionKind kind) {
  StickS3ButtonAction action;
  action.kind = kind;
  return action;
}

inline StickS3ButtonAction makeStickS3SelectionAction(int8_t delta) {
  StickS3ButtonAction action = makeStickS3ButtonAction(
      StickS3ButtonActionKind::SelectionChanged);
  action.selectionDelta = delta;
  return action;
}

inline StickS3ButtonAction makeStickS3KeyboardAction(uint8_t modifier,
                                                      uint8_t key) {
  StickS3ButtonAction action = makeStickS3ButtonAction(
      StickS3ButtonActionKind::KeyboardShortcut);
  action.modifier = modifier;
  action.key = key;
  return action;
}

inline StickS3ButtonAction makeStickS3CodexAction(uint8_t commandIndex) {
  StickS3ButtonAction action =
      makeStickS3ButtonAction(StickS3ButtonActionKind::CodexCommand);
  action.commandIndex = commandIndex;
  return action;
}

inline const char* stickS3CodexCommandKey(uint8_t commandIndex) {
  switch (commandIndex) {
    case 0:
      return "ACT06";
    case 1:
      return "ACT07";
    case 2:
      return "ACT08";
    case 3:
      return "ACT09";
    case 4:
      return "ACT10";
    case 5:
      return "ACT12";
    default:
      return nullptr;
  }
}

inline bool parseStickS3ButtonAction(const char* token,
                                     StickS3ButtonAction& action) {
  if (token == nullptr) {
    return false;
  }
  if (strcmp(token, "none") == 0) {
    action = makeStickS3ButtonAction(StickS3ButtonActionKind::None);
    return true;
  }
  if (strcmp(token, "enter") == 0) {
    action = makeStickS3ButtonAction(StickS3ButtonActionKind::SendEnter);
    return true;
  }
  if (strcmp(token, "right_alt") == 0) {
    action = makeStickS3ButtonAction(StickS3ButtonActionKind::SendRightAlt);
    return true;
  }
  if (strcmp(token, "ctrl_shift_d") == 0) {
    action = makeStickS3KeyboardAction(0x03, 0x07);
    return true;
  }
  if (strcmp(token, "next") == 0) {
    action = makeStickS3SelectionAction(1);
    return true;
  }
  if (strcmp(token, "previous") == 0) {
    action = makeStickS3SelectionAction(-1);
    return true;
  }
  if (strcmp(token, "activate") == 0) {
    action = makeStickS3ButtonAction(
        StickS3ButtonActionKind::ActivateSelectedAgent);
    return true;
  }

  if (strncmp(token, "key:", 4) == 0) {
    unsigned int modifier = 0;
    unsigned int key = 0;
    char trailing = 0;
    if (sscanf(token + 4, "%u:%u%c", &modifier, &key, &trailing) == 2 &&
        modifier <= 0xFF && key <= 0x65 && (modifier != 0 || key != 0)) {
      action = makeStickS3KeyboardAction(static_cast<uint8_t>(modifier),
                                         static_cast<uint8_t>(key));
      return true;
    }
    return false;
  }

  if (strncmp(token, "codex:", 6) == 0) {
    for (uint8_t index = 0; index < 6; ++index) {
      if (strcmp(token + 6, stickS3CodexCommandKey(index)) == 0) {
        action = makeStickS3CodexAction(index);
        return true;
      }
    }
  }
  return false;
}

}  // namespace codex_micro