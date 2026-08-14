// SPDX-License-Identifier: MIT
// Copyright (c) 2026 imliubo

#include <Arduino.h>
#include <M5Unified.h>
#include <Preferences.h>

#include <cmath>

#include "CodexMicroBle.h"
#include "CoreAgentCardStyle.h"
#include "StopWatchAlert.h"
#include "StopWatchNavigateControl.h"
#include "StopWatchSwipe.h"

namespace {

#if defined(ARDUINO_M5STACK_CORES3)
constexpr char kBoardName[] = "CoreS3";
// CoreS3's digitizer ends at the bottom of the panel, so M5Unified never
// reports BtnA/BtnB/BtnC. Page switching relies on the on-screen tab bar.
constexpr bool kHasBottomButtons = false;
// CoreS3 has no vibration motor, so touches are confirmed with a short click.
constexpr bool kHasVibration = false;
#else
constexpr char kBoardName[] = "Core2";
constexpr bool kHasBottomButtons = true;
constexpr bool kHasVibration = true;
#endif

enum class Page : uint8_t { Tasks, Commands, Navigate };
enum class CommandIcon : uint8_t { Download, Approve, Reject, Fork, Mic, Send };
enum class NavigateTouchMode : uint8_t {
  None,
  Joystick,
  DialPress,
  DialRotate,
};

struct TouchAction {
  const char* key = nullptr;
  int8_t agent = -1;
  bool encoderStep = false;
  bool joystick = false;
  float angle = 0.0f;
  uint8_t modifier = 0;
  int8_t pageIndex = -1;
  bool muteToggle = false;

  TouchAction() = default;
  TouchAction(const char* keyValue, int8_t agentValue, bool encoderStepValue,
              bool joystickValue, float angleValue, uint8_t modifierValue = 0,
              int8_t pageIndexValue = -1, bool muteToggleValue = false)
      : key(keyValue),
        agent(agentValue),
        encoderStep(encoderStepValue),
        joystick(joystickValue),
        angle(angleValue),
        modifier(modifierValue),
        pageIndex(pageIndexValue),
        muteToggle(muteToggleValue) {}
};

constexpr uint16_t kBackground = 0x0841;
constexpr uint16_t kPanel = 0x18E3;
constexpr uint16_t kPanelPressed = 0x31A6;
constexpr uint16_t kText = 0xFFFF;
constexpr uint16_t kMuted = 0x9CF3;
constexpr uint16_t kAccent = 0x2E73;
constexpr int kHeaderHeight = 30;
constexpr int kTabHeight = 28;
constexpr int kContentBottom = 240 - kTabHeight;

constexpr int kNavigateCenterY = 118;
constexpr int kJoystickCenterX = 68;
constexpr int kJoystickRadius = 56;
constexpr int kJoystickHandleRadius = 16;
constexpr int kDialCenterX = 196;
constexpr int kDialRadius = 56;
constexpr int kDialPressRadius = 28;
constexpr int kMuteSwitchX = 262;
constexpr int kMuteSwitchY = 62;
constexpr int kMuteSwitchWidth = 50;
constexpr int kMuteSwitchHeight = 112;
constexpr uint32_t kJoystickReportIntervalMs = 32;
constexpr uint32_t kNavigateRedrawIntervalMs = 24;

constexpr uint32_t kTouchActivationDelayMs = 90;
constexpr int kTouchMoveCancelDistance = 12;
constexpr int kSwipeMinimumDistance = 50;
constexpr uint8_t kHapticStrength = 128;
constexpr uint32_t kHapticDurationMs = 35;
constexpr uint8_t kAlertBeepCount = 3;
constexpr uint8_t kAlertVolume = 160;
constexpr float kAlertToneHz = 2200.0f;
constexpr uint32_t kAlertToneMs = 90;
constexpr int kAlertChannel = 0;
constexpr int kClickChannel = 1;
constexpr uint8_t kClickVolume = 70;
constexpr float kClickToneHz = 3400.0f;
constexpr uint32_t kClickToneMs = 12;
constexpr char kCorePreferencesNamespace[] = "codexcore";
constexpr char kMutePreferenceKey[] = "mute";

const char* kAgentKeys[] = {"AG00", "AG01", "AG02", "AG03", "AG04", "AG05"};
// The host folds ACT10 into the double-width ACT10_ACT11 slot, which carries
// the push-to-talk keycap.
const char* kCommandKeys[] = {"ACT06", "ACT07", "ACT08", "ACT09", "ACT10", "ACT12"};
const CommandIcon kCommandIcons[] = {CommandIcon::Download, CommandIcon::Approve,
                                     CommandIcon::Reject, CommandIcon::Fork,
                                     CommandIcon::Mic, CommandIcon::Send};
// The mic keycap holds Right Alt instead of emitting the vendor key event.
constexpr int kMicCommandIndex = 4;

CodexMicroBle codex;
CodexMicroState state;
M5Canvas canvas(&M5.Display);
Page page = Page::Tasks;
TouchAction activeAction;
bool touchActive = false;
uint32_t lastDrawMs = 0;
uint32_t lastBatteryMs = 0;

NavigateTouchMode navigateTouchMode = NavigateTouchMode::None;
codex_micro::StopWatchJoystickPosition joystickPosition;
codex_micro::StopWatchDialGesture dialGesture;
codex_micro::StopWatchBeepSequence beepSequence;
float dialIndicatorAngle = -codex_micro::kStopWatchPi / 2.0f;
float lastSentJoystickAngle = 0.0f;
float lastSentJoystickDistance = -1.0f;
uint32_t lastJoystickReportMs = 0;
bool dialNeedsRebase = false;
bool alertMuted = false;
uint32_t hapticStartedMs = 0;
bool hapticActive = false;
TouchAction pendingTouchAction;
uint32_t touchStartedMs = 0;
bool touchGesturePending = false;
bool touchGestureMoved = false;
bool touchActionCommitted = false;

uint16_t rgb888To565(uint32_t color, float brightness = 1.0f) {
  const uint8_t red = ((color >> 16) & 0xFF) * brightness;
  const uint8_t green = ((color >> 8) & 0xFF) * brightness;
  const uint8_t blue = (color & 0xFF) * brightness;
  return canvas.color565(red, green, blue);
}

void drawCentered(const char* text, int x, int y, int font = 1, uint16_t color = kText) {
  canvas.setTextDatum(middle_center);
  canvas.setTextSize(font);
  canvas.setTextColor(color);
  canvas.drawString(text, x, y);
}

// Core2 drives a motor here; CoreS3 has none, so it clicks instead.
void startHaptic() {
  if (kHasVibration) {
    M5.Power.setVibration(kHapticStrength);
    hapticStartedMs = millis();
    hapticActive = true;
    return;
  }
  if (!alertMuted) {
    M5.Speaker.tone(kClickToneHz, kClickToneMs, kClickChannel, false);
  }
}

void updateHaptic(uint32_t now) {
  if (hapticActive && now - hapticStartedMs >= kHapticDurationMs) {
    M5.Power.setVibration(0);
    hapticActive = false;
  }
}

void loadMutePreference() {
  Preferences preferences;
  if (!preferences.begin(kCorePreferencesNamespace, true)) {
    return;
  }
  alertMuted = preferences.getBool(kMutePreferenceKey, false);
  preferences.end();
}

void saveMutePreference() {
  Preferences preferences;
  if (!preferences.begin(kCorePreferencesNamespace, false)) {
    return;
  }
  preferences.putBool(kMutePreferenceKey, alertMuted);
  preferences.end();
}

void playAlertTone() {
  M5.Speaker.tone(kAlertToneHz, kAlertToneMs, kAlertChannel);
}

void startAlert(codex_micro::StopWatchAlert alert, uint32_t now) {
  Serial.printf("Alert %s%s\n",
                alert == codex_micro::StopWatchAlert::NeedsApproval
                    ? "needs-approval"
                    : "completed",
                alertMuted ? " (muted)" : "");
  if (alertMuted) {
    return;
  }
  if (beepSequence.start(now, kAlertBeepCount)) {
    playAlertTone();
  }
}

void drawHeader() {
  canvas.fillRect(0, 0, 320, kHeaderHeight, kBackground);
  canvas.setTextDatum(middle_left);
  canvas.setTextSize(2);
  canvas.setTextColor(kText);
  canvas.drawString("CODEX MICRO", 8, 15);

  const uint16_t dot = state.connected ? 0x07E0 : 0xF800;
  canvas.fillCircle(278, 15, 4, dot);
  canvas.setTextDatum(middle_right);
  canvas.setTextSize(1);
  canvas.setTextColor(kMuted);
  canvas.drawString(state.connected ? "LIVE" : "PAIR", 316, 15);
}

void drawTabs() {
  const char* labels[] = {"TASKS", "COMMANDS", "NAVIGATE"};
  for (int i = 0; i < 3; ++i) {
    const int x = i * 106 + (i == 2 ? 0 : 1);
    const int width = i == 2 ? 108 : 106;
    const bool selected = static_cast<int>(page) == i;
    canvas.fillRect(x, kContentBottom, width, kTabHeight, selected ? kAccent : kPanel);
    drawCentered(labels[i], x + width / 2, kContentBottom + kTabHeight / 2, 1,
                 selected ? kText : kMuted);
  }
}

void drawButton(int x, int y, int width, int height, const char* label, uint16_t border,
                bool pressed = false, const char* sublabel = nullptr) {
  canvas.fillRoundRect(x, y, width, height, 6, pressed ? kPanelPressed : kPanel);
  canvas.drawRoundRect(x, y, width, height, 6, border);
  drawCentered(label, x + width / 2, y + height / 2 - (sublabel ? 7 : 0),
               strlen(label) > 7 ? 1 : 2, kText);
  if (sublabel != nullptr) {
    drawCentered(sublabel, x + width / 2, y + height / 2 + 13, 1, kMuted);
  }
}

void drawAgentButton(int x, int y, int width, int height, const char* label,
                     uint16_t statusColor, bool assigned, bool pressed,
                     const char* sublabel) {
  const uint16_t fill =
      codex_micro::agentCardFill(statusColor, assigned, pressed, kPanel);
  const uint16_t text =
      codex_micro::agentCardTextColor(fill, kText, kBackground);
  canvas.fillRoundRect(x, y, width, height, 6, fill);
  canvas.drawRoundRect(x, y, width, height, 6, statusColor);
  drawCentered(label, x + width / 2, y + height / 2 - 7, 2, text);
  drawCentered(sublabel, x + width / 2, y + height / 2 + 13, 1, text);
}

void drawIconLine(int x0, int y0, int x1, int y1, uint16_t color) {
  canvas.drawLine(x0, y0, x1, y1, color);
  canvas.drawLine(x0 + 1, y0, x1 + 1, y1, color);
  canvas.drawLine(x0, y0 + 1, x1, y1 + 1, color);
}

void drawCommandIcon(CommandIcon icon, int x, int y, uint16_t color) {
  switch (icon) {
    case CommandIcon::Download:
      drawIconLine(x, y - 15, x, y + 3, color);
      drawIconLine(x - 6, y - 2, x, y + 4, color);
      drawIconLine(x, y + 4, x + 6, y - 2, color);
      drawIconLine(x - 13, y + 5, x - 13, y + 11, color);
      drawIconLine(x - 13, y + 11, x + 13, y + 11, color);
      drawIconLine(x + 13, y + 11, x + 13, y + 5, color);
      break;

    case CommandIcon::Approve:
      canvas.drawCircle(x, y, 14, color);
      canvas.drawCircle(x, y, 13, color);
      drawIconLine(x - 7, y, x - 2, y + 6, color);
      drawIconLine(x - 2, y + 6, x + 8, y - 7, color);
      break;

    case CommandIcon::Reject:
      canvas.drawCircle(x, y, 14, color);
      canvas.drawCircle(x, y, 13, color);
      drawIconLine(x - 6, y - 6, x + 6, y + 6, color);
      drawIconLine(x + 6, y - 6, x - 6, y + 6, color);
      break;

    case CommandIcon::Fork:
      drawIconLine(x - 13, y, x - 3, y, color);
      drawIconLine(x - 3, y, x + 10, y - 11, color);
      drawIconLine(x - 3, y, x + 10, y + 11, color);
      drawIconLine(x + 3, y - 11, x + 10, y - 11, color);
      drawIconLine(x + 10, y - 11, x + 10, y - 4, color);
      drawIconLine(x + 3, y + 11, x + 10, y + 11, color);
      drawIconLine(x + 10, y + 4, x + 10, y + 11, color);
      break;

    case CommandIcon::Mic:
      canvas.drawRoundRect(x - 6, y - 15, 13, 22, 6, color);
      canvas.drawRoundRect(x - 5, y - 14, 11, 20, 5, color);
      drawIconLine(x - 12, y - 2, x - 12, y + 3, color);
      drawIconLine(x - 12, y + 3, x - 7, y + 9, color);
      drawIconLine(x - 7, y + 9, x, y + 11, color);
      drawIconLine(x, y + 11, x + 7, y + 9, color);
      drawIconLine(x + 7, y + 9, x + 12, y + 3, color);
      drawIconLine(x + 12, y + 3, x + 12, y - 2, color);
      drawIconLine(x, y + 11, x, y + 16, color);
      drawIconLine(x - 5, y + 16, x + 5, y + 16, color);
      break;

    case CommandIcon::Send:
      // Scalloped send/assistant glyph from the reference keycap.
      drawIconLine(x - 7, y - 14, x - 2, y - 16, color);
      drawIconLine(x - 2, y - 16, x + 3, y - 14, color);
      drawIconLine(x + 3, y - 14, x + 8, y - 13, color);
      drawIconLine(x + 8, y - 13, x + 10, y - 8, color);
      drawIconLine(x + 10, y - 8, x + 14, y - 4, color);
      drawIconLine(x + 14, y - 4, x + 13, y + 2, color);
      drawIconLine(x + 13, y + 2, x + 11, y + 7, color);
      drawIconLine(x + 11, y + 7, x + 6, y + 9, color);
      drawIconLine(x + 6, y + 9, x + 3, y + 14, color);
      drawIconLine(x + 3, y + 14, x - 3, y + 13, color);
      drawIconLine(x - 3, y + 13, x - 8, y + 11, color);
      drawIconLine(x - 8, y + 11, x - 9, y + 6, color);
      drawIconLine(x - 9, y + 6, x - 13, y + 2, color);
      drawIconLine(x - 13, y + 2, x - 12, y - 4, color);
      drawIconLine(x - 12, y - 4, x - 10, y - 10, color);
      drawIconLine(x - 10, y - 10, x - 7, y - 14, color);
      canvas.fillCircle(x - 5, y - 2, 2, color);
      drawIconLine(x, y + 2, x + 4, y + 3, color);
      drawIconLine(x + 4, y + 3, x + 7, y + 1, color);
      break;
  }
}

void drawCommandButton(int x, int y, int width, int height, CommandIcon icon,
                       uint16_t border, bool pressed) {
  canvas.fillRoundRect(x, y, width, height, 6,
                       pressed ? kPanelPressed : kPanel);
  canvas.drawRoundRect(x, y, width, height, 6, border);
  drawCommandIcon(icon, x + width / 2, y + height / 2, kText);
}

void drawTasks() {
  for (int i = 0; i < 6; ++i) {
    const int row = i / 3;
    const int col = i % 3;
    const int x = 5 + col * 105;
    const int y = 35 + row * 87;
    const ThreadLight& light = state.threads[i];
    float pulse = 1.0f;
    if (light.effect == "breath") {
      pulse = 0.55f + 0.45f * (std::sin(millis() * 0.006f) * 0.5f + 0.5f);
    }
    const bool assigned = light.brightness > 0.01f;
    const uint16_t color =
        assigned ? rgb888To565(light.color, light.brightness * pulse) : 0x4208;
    char title[12];
    snprintf(title, sizeof(title), "AGENT %d", i + 1);
    const char* status = assigned ? light.effect.c_str() : "UNASSIGNED";
    const bool pressed = touchActive && activeAction.agent == i;
    drawAgentButton(x, y, 100, 80, title, color, assigned, pressed, status);
  }
}

void drawCommands() {
  for (int i = 0; i < 6; ++i) {
    const int row = i / 3;
    const int col = i % 3;
    const int x = 5 + col * 105;
    const int y = 35 + row * 87;
    const bool pressed = touchActive && activeAction.key == kCommandKeys[i];
    const uint16_t border = i == 1 ? 0x07E0 : kAccent;
    drawCommandButton(x, y, 100, 80, kCommandIcons[i], border, pressed);
  }
}

void drawNavigate() {
  const bool joystickActive =
      navigateTouchMode == NavigateTouchMode::Joystick;

  drawCentered("JOYSTICK", kJoystickCenterX, 48, 1, kMuted);
  canvas.fillCircle(kJoystickCenterX, kNavigateCenterY, kJoystickRadius,
                    kPanel);
  canvas.drawCircle(kJoystickCenterX, kNavigateCenterY, kJoystickRadius,
                    kAccent);
  canvas.drawLine(kJoystickCenterX - kJoystickRadius + 8, kNavigateCenterY,
                  kJoystickCenterX + kJoystickRadius - 8, kNavigateCenterY,
                  0x4208);
  canvas.drawLine(kJoystickCenterX, kNavigateCenterY - kJoystickRadius + 8,
                  kJoystickCenterX, kNavigateCenterY + kJoystickRadius - 8,
                  0x4208);
  canvas.fillCircle(kJoystickCenterX, kNavigateCenterY - kJoystickRadius + 8, 2,
                    kMuted);
  canvas.fillCircle(kJoystickCenterX + kJoystickRadius - 8, kNavigateCenterY, 2,
                    kMuted);
  canvas.fillCircle(kJoystickCenterX, kNavigateCenterY + kJoystickRadius - 8, 2,
                    kMuted);
  canvas.fillCircle(kJoystickCenterX - kJoystickRadius + 8, kNavigateCenterY, 2,
                    kMuted);

  const float joystickRadians =
      joystickPosition.angle * codex_micro::kStopWatchTwoPi;
  const float handleTravel =
      (kJoystickRadius - kJoystickHandleRadius - 5) *
      (joystickActive ? joystickPosition.distance : 0.0f);
  const int handleX =
      kJoystickCenterX + static_cast<int>(cosf(joystickRadians) * handleTravel);
  const int handleY =
      kNavigateCenterY + static_cast<int>(sinf(joystickRadians) * handleTravel);
  canvas.fillCircle(handleX, handleY, kJoystickHandleRadius,
                    joystickActive ? kAccent : kPanelPressed);
  canvas.drawCircle(handleX, handleY, kJoystickHandleRadius, kText);
  canvas.fillCircle(handleX, handleY, 3, kText);

  const bool dialPressed = navigateTouchMode == NavigateTouchMode::DialPress;
  drawCentered("DIAL", kDialCenterX, 48, 1, kMuted);
  canvas.fillCircle(kDialCenterX, kNavigateCenterY, kDialRadius, 0x1082);
  for (int tick = 0; tick < 24; ++tick) {
    const float angle = tick * codex_micro::kStopWatchTwoPi / 24.0f;
    const int inner = tick % 3 == 0 ? kDialRadius - 9 : kDialRadius - 6;
    canvas.drawLine(
        kDialCenterX + static_cast<int>(cosf(angle) * inner),
        kNavigateCenterY + static_cast<int>(sinf(angle) * inner),
        kDialCenterX + static_cast<int>(cosf(angle) * (kDialRadius - 2)),
        kNavigateCenterY + static_cast<int>(sinf(angle) * (kDialRadius - 2)),
        tick % 3 == 0 ? 0xFFE0 : kMuted);
  }
  canvas.fillCircle(kDialCenterX, kNavigateCenterY, kDialRadius - 11, kPanel);
  canvas.drawCircle(kDialCenterX, kNavigateCenterY, kDialRadius, 0xFFE0);
  canvas.drawCircle(kDialCenterX, kNavigateCenterY, kDialRadius - 11, 0xFFE0);
  canvas.drawLine(
      kDialCenterX + static_cast<int>(cosf(dialIndicatorAngle) * 31),
      kNavigateCenterY + static_cast<int>(sinf(dialIndicatorAngle) * 31),
      kDialCenterX + static_cast<int>(cosf(dialIndicatorAngle) * 43),
      kNavigateCenterY + static_cast<int>(sinf(dialIndicatorAngle) * 43),
      kText);
  canvas.fillCircle(kDialCenterX, kNavigateCenterY, kDialPressRadius,
                    dialPressed ? 0xB5A0 : kPanelPressed);
  canvas.drawCircle(kDialCenterX, kNavigateCenterY, kDialPressRadius,
                    dialPressed ? kText : 0xFFE0);
  drawCentered("PRESS", kDialCenterX, kNavigateCenterY, 1, kText);

  const int trackRadius = kMuteSwitchWidth / 2;
  drawCentered("ALERT", kMuteSwitchX + trackRadius, 48, 1, kMuted);
  canvas.fillRoundRect(kMuteSwitchX, kMuteSwitchY, kMuteSwitchWidth,
                       kMuteSwitchHeight, trackRadius,
                       alertMuted ? kPanel : kAccent);
  canvas.drawRoundRect(kMuteSwitchX, kMuteSwitchY, kMuteSwitchWidth,
                       kMuteSwitchHeight, trackRadius,
                       alertMuted ? kMuted : kText);
  const int knobY = alertMuted
                        ? kMuteSwitchY + kMuteSwitchHeight - trackRadius - 2
                        : kMuteSwitchY + trackRadius + 2;
  canvas.fillCircle(kMuteSwitchX + trackRadius, knobY, trackRadius - 6, kText);
  drawCentered(alertMuted ? "MUTED" : "ON", kMuteSwitchX + trackRadius,
               alertMuted ? kMuteSwitchY + trackRadius + 2
                          : kMuteSwitchY + kMuteSwitchHeight - trackRadius - 2,
               1, alertMuted ? kMuted : kText);
}

void drawScreen() {
  canvas.fillScreen(kBackground);
  drawHeader();
  switch (page) {
    case Page::Tasks:
      drawTasks();
      break;
    case Page::Commands:
      drawCommands();
      break;
    case Page::Navigate:
      drawNavigate();
      break;
  }
  drawTabs();
  canvas.pushSprite(0, 0);
  lastDrawMs = millis();
}

bool inRect(int x, int y, int left, int top, int width, int height) {
  return x >= left && x < left + width && y >= top && y < top + height;
}

TouchAction actionAt(int x, int y) {
  if (y >= kContentBottom) {
    return {nullptr, -1, false, false, 0.0f, 0,
            static_cast<int8_t>(min(2, x / 106))};
  }

  if (page == Page::Tasks) {
    if (x >= 5 && y >= 35) {
      const int col = (x - 5) / 105;
      const int row = (y - 35) / 87;
      if (col < 3 && row < 2 && inRect(x, y, 5 + col * 105, 35 + row * 87, 100, 80)) {
        const int index = row * 3 + col;
        return {kAgentKeys[index], static_cast<int8_t>(index), false, false, 0.0f};
      }
    }
  } else if (page == Page::Commands) {
    if (x >= 5 && y >= 35) {
      const int col = (x - 5) / 105;
      const int row = (y - 35) / 87;
      if (col < 3 && row < 2 && inRect(x, y, 5 + col * 105, 35 + row * 87, 100, 80)) {
        const int index = row * 3 + col;
        const uint8_t modifier =
            index == kMicCommandIndex ? CodexMicroBle::kRightAltModifier : 0;
        return {kCommandKeys[index], -1, false, false, 0.0f, modifier};
      }
    }
  } else {
    if (inRect(x, y, kMuteSwitchX, kMuteSwitchY, kMuteSwitchWidth,
               kMuteSwitchHeight)) {
      return {nullptr, -1, false, false, 0.0f, 0, -1, true};
    }
  }
  return {};
}

void pressAction(const TouchAction& action) {
  activeAction = action;
  touchActive = action.key != nullptr || action.joystick || action.modifier != 0;
  if (!touchActive) return;

  startHaptic();

  if (action.modifier != 0) {
    codex.setModifier(action.modifier);
  } else if (action.joystick) {
    codex.sendJoystick(action.angle, 1.0f);
  } else if (action.encoderStep) {
    codex.sendKey(action.key, 2);
  } else {
    codex.sendKey(action.key, 1, action.agent);
  }
  drawScreen();
}

void releaseAction(bool redraw = true) {
  if (!touchActive) return;
  if (activeAction.modifier != 0) {
    codex.setModifier(0);
  } else if (activeAction.joystick) {
    codex.sendJoystick(activeAction.angle, 0.0f);
  } else if (!activeAction.encoderStep && activeAction.key != nullptr) {
    codex.sendKey(activeAction.key, 0, activeAction.agent);
  }
  touchActive = false;
  activeAction = {};
  if (redraw) {
    drawScreen();
  }
}

bool hasTouchAction(const TouchAction& action) {
  return action.key != nullptr || action.joystick || action.modifier != 0 ||
         action.pageIndex >= 0 || action.muteToggle;
}

void clearTouchGesture() {
  pendingTouchAction = {};
  touchGesturePending = false;
  touchGestureMoved = false;
  touchActionCommitted = false;
}

float joystickAngleDifference(float first, float second) {
  const float difference = fabsf(first - second);
  return difference > 0.5f ? 1.0f - difference : difference;
}

void updateJoystickTouch(int x, int y, uint32_t now, bool forceReport) {
  const auto position = codex_micro::stopWatchJoystickPosition(
      static_cast<float>(x), static_cast<float>(y), kJoystickCenterX,
      kNavigateCenterY, kJoystickRadius);
  joystickPosition = position;

  const bool changed =
      fabsf(position.distance - lastSentJoystickDistance) >= 0.01f ||
      (position.distance > 0.02f &&
       joystickAngleDifference(position.angle, lastSentJoystickAngle) >= 0.01f);
  if (forceReport ||
      (changed && now - lastJoystickReportMs >= kJoystickReportIntervalMs)) {
    codex.sendJoystick(position.angle, position.distance);
    lastSentJoystickAngle = position.angle;
    lastSentJoystickDistance = position.distance;
    lastJoystickReportMs = now;
  }
  if (forceReport || now - lastDrawMs >= kNavigateRedrawIntervalMs) {
    drawScreen();
  }
}

bool beginNavigateTouch(int x, int y, uint32_t now) {
  const float touchX = static_cast<float>(x);
  const float touchY = static_cast<float>(y);
  if (codex_micro::stopWatchPointInCircle(touchX, touchY, kJoystickCenterX,
                                          kNavigateCenterY, kJoystickRadius)) {
    clearTouchGesture();
    navigateTouchMode = NavigateTouchMode::Joystick;
    startHaptic();
    updateJoystickTouch(x, y, now, true);
    return true;
  }
  if (!codex_micro::stopWatchPointInCircle(touchX, touchY, kDialCenterX,
                                           kNavigateCenterY, kDialRadius)) {
    return false;
  }

  clearTouchGesture();
  if (codex_micro::stopWatchPointInCircle(touchX, touchY, kDialCenterX,
                                          kNavigateCenterY,
                                          kDialPressRadius)) {
    navigateTouchMode = NavigateTouchMode::DialPress;
    pressAction({"ENC", -1, false, false, 0.0f});
  } else {
    navigateTouchMode = NavigateTouchMode::DialRotate;
    dialGesture.begin(touchX, touchY, kDialCenterX, kNavigateCenterY);
    dialIndicatorAngle =
        atan2f(touchY - kNavigateCenterY, touchX - kDialCenterX);
    dialNeedsRebase = false;
    drawScreen();
  }
  return true;
}

void finishNavigateTouch(bool redraw = true) {
  const NavigateTouchMode mode = navigateTouchMode;
  if (mode == NavigateTouchMode::None) {
    return;
  }
  navigateTouchMode = NavigateTouchMode::None;
  dialGesture.reset();
  dialNeedsRebase = false;

  if (mode == NavigateTouchMode::Joystick) {
    codex.sendJoystick(joystickPosition.angle, 0.0f);
    joystickPosition.distance = 0.0f;
    lastSentJoystickDistance = 0.0f;
  } else if (mode == NavigateTouchMode::DialPress) {
    releaseAction(redraw);
    return;
  }
  if (redraw) {
    drawScreen();
  }
}

void updateNavigateTouch(const m5::touch_detail_t& touch, uint32_t now) {
  if (navigateTouchMode == NavigateTouchMode::None) {
    return;
  }
  if (touch.isPressed()) {
    if (navigateTouchMode == NavigateTouchMode::Joystick) {
      updateJoystickTouch(touch.x, touch.y, now, false);
    } else if (navigateTouchMode == NavigateTouchMode::DialRotate) {
      const float touchX = static_cast<float>(touch.x);
      const float touchY = static_cast<float>(touch.y);
      if (codex_micro::stopWatchPointInCircle(touchX, touchY, kDialCenterX,
                                              kNavigateCenterY,
                                              kDialPressRadius)) {
        dialNeedsRebase = true;
        return;
      }
      if (dialNeedsRebase) {
        dialGesture.begin(touchX, touchY, kDialCenterX, kNavigateCenterY);
        dialNeedsRebase = false;
      }
      const int steps =
          dialGesture.update(touchX, touchY, kDialCenterX, kNavigateCenterY);
      dialIndicatorAngle =
          atan2f(touchY - kNavigateCenterY, touchX - kDialCenterX);
      if (steps != 0) {
        const char* key = steps > 0 ? "ENC_CW" : "ENC_CC";
        for (int step = 0; step < abs(steps); ++step) {
          codex.sendKey(key, 2);
        }
        startHaptic();
      }
      if (now - lastDrawMs >= kNavigateRedrawIntervalMs) {
        drawScreen();
      }
    }
    return;
  }
  if (touch.wasReleased()) {
    finishNavigateTouch();
  }
}

void selectPage(int pageIndex) {
  startHaptic();
  finishNavigateTouch(false);
  releaseAction(false);
  page = static_cast<Page>(pageIndex);
  drawScreen();
}

void toggleMute() {
  alertMuted = !alertMuted;
  if (alertMuted) {
    beepSequence.stop();
  }
  saveMutePreference();
  startHaptic();
  drawScreen();
}

void triggerTouchAction(const TouchAction& action) {
  if (action.pageIndex >= 0) {
    selectPage(action.pageIndex);
  } else if (action.muteToggle) {
    toggleMute();
  } else {
    pressAction(action);
  }
}

void changePage(int delta) {
  const int pageCount = 3;
  const int next = (static_cast<int>(page) + delta + pageCount) % pageCount;
  clearTouchGesture();
  startHaptic();
  finishNavigateTouch(false);
  releaseAction(false);
  page = static_cast<Page>(next);
  drawScreen();
}

void beginTouchGesture(const TouchAction& action, uint32_t now) {
  pendingTouchAction = action;
  touchStartedMs = now;
  touchGesturePending = true;
  touchGestureMoved = false;
  touchActionCommitted = false;
}

void updateTouchGesture(const m5::touch_detail_t& touch, uint32_t now) {
  if (!touchGesturePending) {
    return;
  }

  if (touch.isPressed()) {
    if (!touchGestureMoved &&
        (abs(touch.distanceX()) > kTouchMoveCancelDistance ||
         abs(touch.distanceY()) > kTouchMoveCancelDistance)) {
      touchGestureMoved = true;
      if (touchActionCommitted) {
        releaseAction();
      }
    }

    if (!touchGestureMoved && !touchActionCommitted &&
        pendingTouchAction.pageIndex < 0 && !pendingTouchAction.muteToggle &&
        now - touchStartedMs >= kTouchActivationDelayMs) {
      triggerTouchAction(pendingTouchAction);
      touchActionCommitted = true;
    }
    return;
  }

  if (!touch.wasReleased()) {
    return;
  }

  const TouchAction action = pendingTouchAction;
  const bool moved = touchGestureMoved;
  const bool committed = touchActionCommitted;
  const int8_t swipeDelta = codex_micro::stopWatchSwipePageDelta(
      touch.distanceX(), touch.distanceY(), kSwipeMinimumDistance);
  clearTouchGesture();

  if (committed) {
    releaseAction();
  }
  if (swipeDelta != 0) {
    changePage(swipeDelta);
  } else if (!moved && !committed && hasTouchAction(action)) {
    triggerTouchAction(action);
    if (touchActive) {
      delay(8);
      releaseAction();
    }
  }
}

void updateBattery() {
  if (millis() - lastBatteryMs < 30000 && lastBatteryMs != 0) return;
  lastBatteryMs = millis();
  const int level = M5.Power.getBatteryLevel();
  const bool charging = M5.Power.isCharging();
  codex.setBattery(level < 0 ? 100 : static_cast<uint8_t>(level), charging);
}

}  // namespace

void setup() {
  Serial.begin(115200);
#if ARDUINO_USB_CDC_ON_BOOT
  // Native-USB targets need time for the host to enumerate the CDC port.
  delay(600);
#else
  delay(100);
#endif
  Serial.printf("Codex Micro %s boot\n", kBoardName);

  auto config = M5.config();
  config.clear_display = true;
  M5.begin(config);
  M5.Power.setVibration(0);
  M5.Display.setRotation(1);
  M5.Display.setBrightness(120);
  M5.Display.setTextWrap(false);
  M5.Speaker.setVolume(kAlertVolume);
  M5.Speaker.setChannelVolume(kClickChannel, kClickVolume);
  loadMutePreference();

  canvas.setColorDepth(16);
  if (canvas.createSprite(M5.Display.width(), M5.Display.height()) == nullptr) {
    M5.Display.fillScreen(TFT_BLACK);
    M5.Display.setTextColor(TFT_RED);
    M5.Display.setTextDatum(middle_center);
    M5.Display.drawString("Canvas allocation failed", 160, 120);
    Serial.println("Canvas allocation failed");
    while (true) delay(1000);
  }
  canvas.setTextWrap(false);

  codex.begin();
  state = codex.snapshot();
  updateBattery();
  drawScreen();
  Serial.println("CODEX_MICRO_READY");
}

void loop() {
  M5.update();
  codex.poll();
  const auto touch = M5.Touch.getDetail();
  if (touch.wasPressed()) {
    const uint32_t touchNow = millis();
    if (page != Page::Navigate ||
        !beginNavigateTouch(touch.x, touch.y, touchNow)) {
      beginTouchGesture(actionAt(touch.x, touch.y), touchNow);
    }
  }
  if (navigateTouchMode != NavigateTouchMode::None) {
    updateNavigateTouch(touch, millis());
  } else {
    updateTouchGesture(touch, millis());
  }

  if (kHasBottomButtons) {
    if (M5.BtnA.wasPressed()) {
      selectPage(0);
    } else if (M5.BtnB.wasPressed()) {
      selectPage(1);
    } else if (M5.BtnC.wasPressed()) {
      selectPage(2);
    }
  }

  CodexMicroState latest = codex.snapshot();
  const codex_micro::StopWatchAlert alert =
      codex_micro::stopWatchAlertForChange(state.threads, latest.threads);
  if (latest.dirty || latest.connected != state.connected) {
    state = latest;
    drawScreen();
  } else {
    state = latest;
  }

  if (alert != codex_micro::StopWatchAlert::None) {
    startAlert(alert, millis());
  }
  if (beepSequence.update(millis())) {
    playAlertTone();
  }

  if (page == Page::Tasks && millis() - lastDrawMs > 80) {
    bool animated = false;
    for (const ThreadLight& light : state.threads) {
      animated = animated || light.effect == "breath";
    }
    if (animated) drawScreen();
  }

  updateHaptic(millis());
  updateBattery();
  delay(8);
}
