// SPDX-License-Identifier: MIT
// Copyright (c) 2026 imliubo

#include <Arduino.h>
#include <M5Unified.h>

#include <cmath>

#include "CodexMicroBle.h"
#include "CoreAgentCardStyle.h"
#include "StopWatchButtonController.h"
#include "StopWatchSwipe.h"

#if !defined(CODEX_MICRO_STOPWATCH)
#error "main_stopwatch.cpp is only for the m5stack-stopwatch environment"
#endif

namespace {

enum class Page : uint8_t { Tasks, Commands, Navigate };
enum class CommandIcon : uint8_t { Download, Approve, Reject, Fork, Mic, Send };

struct TouchAction {
  const char* key = nullptr;
  int8_t agent = -1;
  bool encoderStep = false;
  bool joystick = false;
  float angle = 0.0f;
  uint8_t modifier = 0;
  int8_t pageIndex = -1;

  TouchAction() = default;
  TouchAction(const char* keyValue, int8_t agentValue, bool encoderStepValue,
              bool joystickValue, float angleValue,
              uint8_t modifierValue = 0, int8_t pageIndexValue = -1)
      : key(keyValue),
        agent(agentValue),
        encoderStep(encoderStepValue),
        joystick(joystickValue),
        angle(angleValue),
        modifier(modifierValue),
        pageIndex(pageIndexValue) {}
};

constexpr uint16_t kBackground = 0x0841;
constexpr uint16_t kPanel = 0x18E3;
constexpr uint16_t kPanelPressed = 0x31A6;
constexpr uint16_t kText = 0xFFFF;
constexpr uint16_t kMuted = 0x9CF3;
constexpr uint16_t kAccent = 0x2E73;
constexpr uint16_t kConnected = 0x07E0;
constexpr uint16_t kDisconnected = 0xF800;
constexpr uint8_t kHapticStrength = 128;
constexpr uint32_t kHapticDurationMs = 35;
constexpr uint32_t kTouchActivationDelayMs = 90;
constexpr int kTouchMoveCancelDistance = 16;
constexpr int kSwipeMinimumDistance = 70;
constexpr int kDesignSize = 468;
constexpr int kHeaderCenterY = 46;
constexpr int kTabY = 401;
constexpr int kTabX = 123;
constexpr int kTabWidth = 72;
constexpr int kTabGap = 2;
constexpr int kTabHeight = 38;

constexpr int kGridX = 54;
constexpr int kGridY = 82;
constexpr int kGridColumnStep = 180;
constexpr int kGridRowStep = 103;
constexpr int kGridButtonWidth = 180;
constexpr int kGridButtonHeight = 88;

const char* kAgentKeys[] = {"AG00", "AG01", "AG02",
                            "AG03", "AG04", "AG05"};
const char* kCommandKeys[] = {"ACT06", "ACT07", "ACT08",
                              "ACT09", "ACT10", "ACT12"};
const char* kCommandLabels[] = {"FAST", "APPROVE", "REJECT",
                                "NEW CHAT", "MIC", "SEND"};
const CommandIcon kCommandIcons[] = {
    CommandIcon::Download, CommandIcon::Approve, CommandIcon::Reject,
    CommandIcon::Fork, CommandIcon::Mic, CommandIcon::Send};
constexpr int kMicCommandIndex = 4;

CodexMicroBle codex;
CodexMicroState state;
M5Canvas canvas(&M5.Display);
codex_micro::StopWatchButtonController physicalButtons;
Page page = Page::Tasks;
TouchAction activeAction;
bool touchActive = false;
uint32_t lastDrawMs = 0;
uint32_t lastBatteryMs = 0;
uint32_t hapticStartedMs = 0;
bool hapticActive = false;
TouchAction pendingTouchAction;
uint32_t touchStartedMs = 0;
bool touchGesturePending = false;
bool touchGestureMoved = false;
bool touchActionCommitted = false;

int scaleX(int value) {
  return value * canvas.width() / kDesignSize;
}

int scaleY(int value) {
  return value * canvas.height() / kDesignSize;
}

uint16_t rgb888To565(uint32_t color, float brightness = 1.0f) {
  brightness = constrain(brightness, 0.0f, 1.0f);
  const uint8_t red = ((color >> 16) & 0xFF) * brightness;
  const uint8_t green = ((color >> 8) & 0xFF) * brightness;
  const uint8_t blue = (color & 0xFF) * brightness;
  return canvas.color565(red, green, blue);
}

void startHaptic() {
  M5.Power.setVibration(kHapticStrength);
  hapticStartedMs = millis();
  hapticActive = true;
}

void updateHaptic(uint32_t now) {
  if (hapticActive && now - hapticStartedMs >= kHapticDurationMs) {
    M5.Power.setVibration(0);
    hapticActive = false;
  }
}

void drawCentered(const char* text, int x, int y, int font = 1,
                  uint16_t color = kText) {
  canvas.setTextDatum(middle_center);
  canvas.setTextSize(font);
  canvas.setTextColor(color);
  canvas.drawString(text, scaleX(x), scaleY(y));
}

void drawHeader() {
  const int center = canvas.width() / 2;
  canvas.setTextDatum(middle_center);
  canvas.setTextSize(2);
  canvas.setTextColor(kText);
  canvas.drawString("CODEX MICRO", center, scaleY(kHeaderCenterY));

  canvas.fillCircle(scaleX(350), scaleY(kHeaderCenterY), scaleX(5),
                    state.connected ? kConnected : kDisconnected);
}

void drawTabs() {
  const char* labels[] = {"TASKS", "COMMANDS", "NAV"};
  for (int index = 0; index < 3; ++index) {
    const int x = kTabX + index * (kTabWidth + kTabGap);
    const bool selected = static_cast<int>(page) == index;
    canvas.fillRoundRect(scaleX(x), scaleY(kTabY), scaleX(kTabWidth),
                         scaleY(kTabHeight), scaleX(9),
                         selected ? kAccent : kPanel);
    canvas.drawRoundRect(scaleX(x), scaleY(kTabY), scaleX(kTabWidth),
                         scaleY(kTabHeight), scaleX(9),
                         selected ? kText : kMuted);
    drawCentered(labels[index], x + kTabWidth / 2,
                 kTabY + kTabHeight / 2, 1,
                 selected ? kText : kMuted);
  }
}

void drawButton(int x, int y, int width, int height, const char* label,
                uint16_t border, bool pressed = false,
                const char* sublabel = nullptr) {
  canvas.fillRoundRect(scaleX(x), scaleY(y), scaleX(width), scaleY(height),
                       scaleX(10), pressed ? kPanelPressed : kPanel);
  canvas.drawRoundRect(scaleX(x), scaleY(y), scaleX(width), scaleY(height),
                       scaleX(10), border);
  drawCentered(label, x + width / 2,
               y + height / 2 - (sublabel ? 10 : 0),
               strlen(label) > 8 ? 1 : 2, kText);
  if (sublabel != nullptr) {
    drawCentered(sublabel, x + width / 2, y + height / 2 + 18, 1,
                 kMuted);
  }
}

void drawAgentButton(int x, int y, int width, int height, const char* label,
                     uint16_t statusColor, bool assigned, bool pressed,
                     const char* sublabel) {
  const uint16_t fill =
      codex_micro::agentCardFill(statusColor, assigned, pressed, kPanel);
  const uint16_t text =
      codex_micro::agentCardTextColor(fill, kText, kBackground);
  canvas.fillRoundRect(scaleX(x), scaleY(y), scaleX(width), scaleY(height),
                       scaleX(10), fill);
  canvas.drawRoundRect(scaleX(x), scaleY(y), scaleX(width), scaleY(height),
                       scaleX(10), statusColor);
  drawCentered(label, x + width / 2, y + height / 2 - 12, 2, text);
  drawCentered(sublabel, x + width / 2, y + height / 2 + 20, 1, text);
}

void drawIconLine(int x0, int y0, int x1, int y1, uint16_t color) {
  const int scaledX0 = scaleX(x0);
  const int scaledY0 = scaleY(y0);
  const int scaledX1 = scaleX(x1);
  const int scaledY1 = scaleY(y1);
  canvas.drawLine(scaledX0, scaledY0, scaledX1, scaledY1, color);
  canvas.drawLine(scaledX0 + 1, scaledY0, scaledX1 + 1, scaledY1, color);
  canvas.drawLine(scaledX0, scaledY0 + 1, scaledX1, scaledY1 + 1, color);
}

void drawCommandIcon(CommandIcon icon, int x, int y, uint16_t color) {
  switch (icon) {
    case CommandIcon::Download:
      drawIconLine(x, y - 18, x, y + 5, color);
      drawIconLine(x - 8, y - 2, x, y + 6, color);
      drawIconLine(x, y + 6, x + 8, y - 2, color);
      drawIconLine(x - 16, y + 8, x - 16, y + 16, color);
      drawIconLine(x - 16, y + 16, x + 16, y + 16, color);
      drawIconLine(x + 16, y + 16, x + 16, y + 8, color);
      break;
    case CommandIcon::Approve:
      canvas.drawCircle(scaleX(x), scaleY(y), scaleX(18), color);
      canvas.drawCircle(scaleX(x), scaleY(y), scaleX(17), color);
      drawIconLine(x - 9, y, x - 3, y + 7, color);
      drawIconLine(x - 3, y + 7, x + 11, y - 10, color);
      break;
    case CommandIcon::Reject:
      canvas.drawCircle(scaleX(x), scaleY(y), scaleX(18), color);
      canvas.drawCircle(scaleX(x), scaleY(y), scaleX(17), color);
      drawIconLine(x - 9, y - 9, x + 9, y + 9, color);
      drawIconLine(x + 9, y - 9, x - 9, y + 9, color);
      break;
    case CommandIcon::Fork:
      drawIconLine(x - 16, y, x - 4, y, color);
      drawIconLine(x - 4, y, x + 13, y - 14, color);
      drawIconLine(x - 4, y, x + 13, y + 14, color);
      drawIconLine(x + 5, y - 14, x + 13, y - 14, color);
      drawIconLine(x + 13, y - 14, x + 13, y - 6, color);
      drawIconLine(x + 5, y + 14, x + 13, y + 14, color);
      drawIconLine(x + 13, y + 6, x + 13, y + 14, color);
      break;
    case CommandIcon::Mic:
      canvas.drawRoundRect(scaleX(x - 8), scaleY(y - 18), scaleX(16),
                           scaleY(27), scaleX(8), color);
      drawIconLine(x - 15, y - 2, x - 15, y + 5, color);
      drawIconLine(x - 15, y + 5, x - 8, y + 12, color);
      drawIconLine(x - 8, y + 12, x, y + 14, color);
      drawIconLine(x, y + 14, x + 8, y + 12, color);
      drawIconLine(x + 8, y + 12, x + 15, y + 5, color);
      drawIconLine(x + 15, y + 5, x + 15, y - 2, color);
      drawIconLine(x, y + 14, x, y + 20, color);
      drawIconLine(x - 7, y + 20, x + 7, y + 20, color);
      break;
    case CommandIcon::Send:
      canvas.fillCircle(scaleX(x), scaleY(y), scaleX(20), color);
      drawIconLine(x - 9, y, x - 2, y + 6, kBackground);
      drawIconLine(x - 2, y + 6, x + 11, y - 8, kBackground);
      break;
  }
}

void drawCommandButton(int x, int y, int width, int height,
                       CommandIcon icon, const char* label, uint16_t border,
                       bool pressed) {
  canvas.fillRoundRect(scaleX(x), scaleY(y), scaleX(width), scaleY(height),
                       scaleX(10), pressed ? kPanelPressed : kPanel);
  canvas.drawRoundRect(scaleX(x), scaleY(y), scaleX(width), scaleY(height),
                       scaleX(10), border);
  drawCommandIcon(icon, x + width / 2, y + height / 2 - 10, kText);
  drawCentered(label, x + width / 2, y + height - 16, 1, kMuted);
}

void drawTasks() {
  for (int index = 0; index < 6; ++index) {
    const int row = index / 2;
    const int column = index % 2;
    const int x = kGridX + column * kGridColumnStep;
    const int y = kGridY + row * kGridRowStep;
    const ThreadLight& light = state.threads[index];
    float pulse = 1.0f;
    if (light.effect == "breath") {
      pulse = 0.55f +
              0.45f * (std::sin(millis() * 0.006f) * 0.5f + 0.5f);
    }
    const bool assigned = light.brightness > 0.01f;
    const uint16_t color =
        assigned ? rgb888To565(light.color, light.brightness * pulse) : 0x4208;
    char title[12];
    snprintf(title, sizeof(title), "AGENT %d", index + 1);
    const char* status = assigned ? light.effect.c_str() : "UNASSIGNED";
    const bool pressed = touchActive && activeAction.agent == index;
    drawAgentButton(x, y, kGridButtonWidth, kGridButtonHeight, title, color,
                    assigned, pressed, status);
  }
}

void drawCommands() {
  for (int index = 0; index < 6; ++index) {
    const int row = index / 2;
    const int column = index % 2;
    const int x = kGridX + column * kGridColumnStep;
    const int y = kGridY + row * kGridRowStep;
    const bool pressed =
        touchActive && activeAction.key == kCommandKeys[index];
    drawCommandButton(x, y, kGridButtonWidth, kGridButtonHeight,
                      kCommandIcons[index], kCommandLabels[index],
                      index == 1 ? kConnected : kAccent, pressed);
  }
}

void drawNavigate() {
  drawButton(174, 83, 120, 60, "UP", kAccent,
             touchActive && activeAction.joystick &&
                 activeAction.angle == 0.75f,
             "PLAN");
  drawButton(53, 157, 120, 64, "LEFT", kAccent,
             touchActive && activeAction.joystick &&
                 activeAction.angle == 0.5f,
             "BACK");
  drawButton(295, 157, 120, 64, "RIGHT", kAccent,
             touchActive && activeAction.joystick &&
                 activeAction.angle == 0.0f,
             "FORWARD");
  drawButton(174, 235, 120, 60, "DOWN", kAccent,
             touchActive && activeAction.joystick &&
                 activeAction.angle == 0.25f,
             "SIDEBAR");

  drawButton(68, 315, 104, 68, "CCW", 0xFFE0,
             touchActive && activeAction.key != nullptr &&
                 strcmp(activeAction.key, "ENC_CC") == 0,
             "NEXT");
  drawButton(296, 315, 104, 68, "CW", 0xFFE0,
             touchActive && activeAction.key != nullptr &&
                 strcmp(activeAction.key, "ENC_CW") == 0,
             "PREV");
  drawButton(182, 309, 104, 78, "DIAL", 0xFFE0,
             touchActive && activeAction.key != nullptr &&
                 strcmp(activeAction.key, "ENC") == 0,
             "SETTINGS");
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
  return x >= scaleX(left) && x < scaleX(left + width) &&
         y >= scaleY(top) && y < scaleY(top + height);
}

TouchAction actionAt(int x, int y) {
  for (int index = 0; index < 3; ++index) {
    const int tabX = kTabX + index * (kTabWidth + kTabGap);
    if (inRect(x, y, tabX, kTabY, kTabWidth, kTabHeight)) {
      return {nullptr, -1, false, false, 0.0f, 0,
              static_cast<int8_t>(index)};
    }
  }

  if (page == Page::Tasks || page == Page::Commands) {
    for (int index = 0; index < 6; ++index) {
      const int row = index / 2;
      const int column = index % 2;
      const int buttonX = kGridX + column * kGridColumnStep;
      const int buttonY = kGridY + row * kGridRowStep;
      if (!inRect(x, y, buttonX, buttonY, kGridButtonWidth,
                  kGridButtonHeight)) {
        continue;
      }
      if (page == Page::Tasks) {
        return {kAgentKeys[index], static_cast<int8_t>(index), false, false,
                0.0f};
      }
      const uint8_t modifier = index == kMicCommandIndex
                                   ? CodexMicroBle::kRightAltModifier
                                   : 0;
      return {kCommandKeys[index], -1, false, false, 0.0f, modifier};
    }
    return {};
  }

  if (inRect(x, y, 174, 83, 120, 60)) {
    return {nullptr, -1, false, true, 0.75f};
  }
  if (inRect(x, y, 53, 157, 120, 64)) {
    return {nullptr, -1, false, true, 0.5f};
  }
  if (inRect(x, y, 295, 157, 120, 64)) {
    return {nullptr, -1, false, true, 0.0f};
  }
  if (inRect(x, y, 174, 235, 120, 60)) {
    return {nullptr, -1, false, true, 0.25f};
  }
  if (inRect(x, y, 68, 315, 104, 68)) {
    return {"ENC_CC", -1, true, false, 0.0f};
  }
  if (inRect(x, y, 296, 315, 104, 68)) {
    return {"ENC_CW", -1, true, false, 0.0f};
  }
  if (inRect(x, y, 182, 309, 104, 78)) {
    return {"ENC", -1, false, false, 0.0f};
  }
  return {};
}

void pressAction(const TouchAction& action) {
  activeAction = action;
  touchActive = action.key != nullptr || action.joystick ||
                action.modifier != 0;
  if (!touchActive) {
    return;
  }

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

void releaseAction() {
  if (!touchActive) {
    return;
  }
  if (activeAction.modifier != 0) {
    codex.setModifier(0);
  } else if (activeAction.joystick) {
    codex.sendJoystick(activeAction.angle, 0.0f);
  } else if (!activeAction.encoderStep && activeAction.key != nullptr) {
    codex.sendKey(activeAction.key, 0, activeAction.agent);
  }
  touchActive = false;
  activeAction = {};
  drawScreen();
}

bool hasTouchAction(const TouchAction& action) {
  return action.key != nullptr || action.joystick || action.modifier != 0 ||
         action.pageIndex >= 0;
}

void clearTouchGesture() {
  pendingTouchAction = {};
  touchGesturePending = false;
  touchGestureMoved = false;
  touchActionCommitted = false;
}

void selectPage(int pageIndex) {
  startHaptic();
  releaseAction();
  page = static_cast<Page>(pageIndex);
  drawScreen();
}

void triggerTouchAction(const TouchAction& action) {
  if (action.pageIndex >= 0) {
    selectPage(action.pageIndex);
  } else {
    pressAction(action);
  }
}

void changePage(int delta) {
  const int pageCount = 3;
  const int next =
      (static_cast<int>(page) + delta + pageCount) % pageCount;
  clearTouchGesture();
  startHaptic();
  releaseAction();
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
    const int distanceX = touch.distanceX();
    const int distanceY = touch.distanceY();
    if (!touchGestureMoved &&
        (abs(distanceX) > kTouchMoveCancelDistance ||
         abs(distanceY) > kTouchMoveCancelDistance)) {
      touchGestureMoved = true;
      if (touchActionCommitted) {
        releaseAction();
      }
    }

    if (!touchGestureMoved && !touchActionCommitted &&
        pendingTouchAction.pageIndex < 0 &&
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

void handlePhysicalButtonAction(
    codex_micro::StopWatchButtonAction action) {
  switch (action) {
    case codex_micro::StopWatchButtonAction::ButtonASingle:
      startHaptic();
      codex.sendRightAlt();
      break;
    case codex_micro::StopWatchButtonAction::ButtonADouble:
      changePage(-1);
      break;
    case codex_micro::StopWatchButtonAction::ButtonBSingle:
      startHaptic();
      codex.sendEnter();
      break;
    case codex_micro::StopWatchButtonAction::ButtonBDouble:
      changePage(1);
      break;
    case codex_micro::StopWatchButtonAction::None:
      break;
  }
}

void updateBattery() {
  if (lastBatteryMs != 0 && millis() - lastBatteryMs < 30000) {
    return;
  }
  lastBatteryMs = millis();
  const int level = M5.Power.getBatteryLevel();
  codex.setBattery(level < 0 ? 100 : static_cast<uint8_t>(level),
                   M5.Power.isCharging());
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(600);
  Serial.println("Codex Micro StopWatch boot");

  auto config = M5.config();
  config.clear_display = true;
  M5.begin(config);
  M5.Power.setVibration(0);
  M5.Display.setRotation(0);
  M5.Display.setBrightness(100);
  M5.Display.setTextWrap(false);

  if (M5.getBoard() != m5::board_t::board_M5StopWatch) {
    Serial.printf("Unexpected board id=%u\n",
                  static_cast<unsigned>(M5.getBoard()));
  }

  canvas.setColorDepth(16);
  if (canvas.createSprite(M5.Display.width(), M5.Display.height()) == nullptr) {
    M5.Display.fillScreen(TFT_BLACK);
    M5.Display.setTextColor(TFT_RED);
    M5.Display.setTextDatum(middle_center);
    M5.Display.drawString("Canvas allocation failed",
                          M5.Display.width() / 2,
                          M5.Display.height() / 2);
    Serial.println("Canvas allocation failed");
    while (true) {
      delay(1000);
    }
  }
  canvas.setTextWrap(false);

  codex.begin();
  state = codex.snapshot();
  updateBattery();
  drawScreen();
  Serial.printf("STOPWATCH_READY board=%u display=%dx%d\n",
                static_cast<unsigned>(M5.getBoard()), M5.Display.width(),
                M5.Display.height());
  Serial.println("CODEX_MICRO_READY");
}

void loop() {
  M5.update();
  codex.poll();

  const auto touch = M5.Touch.getDetail();
  if (touch.wasPressed()) {
    beginTouchGesture(actionAt(touch.x, touch.y), millis());
  }
  updateTouchGesture(touch, millis());

  if (M5.BtnA.wasReleased()) {
    handlePhysicalButtonAction(
        physicalButtons.onButtonAReleased(millis()));
  }
  if (M5.BtnB.wasReleased()) {
    handlePhysicalButtonAction(
        physicalButtons.onButtonBReleased(millis()));
  }

  const uint32_t buttonNow = millis();
  handlePhysicalButtonAction(
      physicalButtons.pollButtonA(buttonNow, M5.BtnA.isPressed()));
  handlePhysicalButtonAction(
      physicalButtons.pollButtonB(buttonNow, M5.BtnB.isPressed()));

  CodexMicroState latest = codex.snapshot();
  if (latest.dirty || latest.connected != state.connected) {
    state = latest;
    drawScreen();
  } else {
    state = latest;
  }

  if (page == Page::Tasks && millis() - lastDrawMs > 80) {
    bool animated = false;
    for (const ThreadLight& light : state.threads) {
      animated = animated || light.effect == "breath";
    }
    if (animated) {
      drawScreen();
    }
  }

  updateHaptic(millis());
  updateBattery();
  delay(8);
}