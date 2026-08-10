// SPDX-License-Identifier: MIT
// Copyright (c) 2026 imliubo

#include <Arduino.h>
#include <M5Unified.h>

#include <cmath>

#include "CodexMicroBle.h"
#include "CoreAgentCardStyle.h"

namespace {

#if defined(ARDUINO_M5STACK_CORES3)
constexpr char kBoardName[] = "CoreS3";
// CoreS3's digitizer ends at the bottom of the panel, so M5Unified never
// reports BtnA/BtnB/BtnC. Page switching relies on the on-screen tab bar.
constexpr bool kHasBottomButtons = false;
#else
constexpr char kBoardName[] = "Core2";
constexpr bool kHasBottomButtons = true;
#endif

enum class Page : uint8_t { Tasks, Commands, Navigate };
enum class CommandIcon : uint8_t { Download, Approve, Reject, Fork, Mic, Send };

struct TouchAction {
  const char* key = nullptr;
  int8_t agent = -1;
  bool encoderStep = false;
  bool joystick = false;
  float angle = 0.0f;
  uint8_t modifier = 0;

  TouchAction() = default;
  TouchAction(const char* keyValue, int8_t agentValue, bool encoderStepValue,
              bool joystickValue, float angleValue, uint8_t modifierValue = 0)
      : key(keyValue),
        agent(agentValue),
        encoderStep(encoderStepValue),
        joystick(joystickValue),
        angle(angleValue),
        modifier(modifierValue) {}
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
  drawButton(18, 42, 70, 48, "UP", kAccent, touchActive && activeAction.joystick && activeAction.angle == 0.75f, "PLAN");
  drawButton(18, 148, 70, 48, "DOWN", kAccent, touchActive && activeAction.joystick && activeAction.angle == 0.25f, "SIDEBAR");
  drawButton(2, 95, 70, 48, "LEFT", kAccent, touchActive && activeAction.joystick && activeAction.angle == 0.5f, "BACK");
  drawButton(76, 95, 70, 48, "RIGHT", kAccent, touchActive && activeAction.joystick && activeAction.angle == 0.0f, "FORWARD");

  drawButton(166, 42, 68, 64, "CCW", 0xFFE0,
             touchActive && activeAction.key != nullptr && strcmp(activeAction.key, "ENC_CC") == 0,
             "NEXT");
  drawButton(244, 42, 68, 64, "CW", 0xFFE0,
             touchActive && activeAction.key != nullptr && strcmp(activeAction.key, "ENC_CW") == 0,
             "PREV");
  drawButton(166, 118, 146, 78, "DIAL", 0xFFE0,
             touchActive && activeAction.key != nullptr && strcmp(activeAction.key, "ENC") == 0,
             "TAP / HOLD SETTINGS");
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
    page = static_cast<Page>(min(2, x / 106));
    drawScreen();
    return {};
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
    if (inRect(x, y, 18, 42, 70, 48)) return {nullptr, -1, false, true, 0.75f};
    if (inRect(x, y, 18, 148, 70, 48)) return {nullptr, -1, false, true, 0.25f};
    if (inRect(x, y, 2, 95, 70, 48)) return {nullptr, -1, false, true, 0.5f};
    if (inRect(x, y, 76, 95, 70, 48)) return {nullptr, -1, false, true, 0.0f};
    if (inRect(x, y, 166, 42, 68, 64)) return {"ENC_CC", -1, true, false, 0.0f};
    if (inRect(x, y, 244, 42, 68, 64)) return {"ENC_CW", -1, true, false, 0.0f};
    if (inRect(x, y, 166, 118, 146, 78)) return {"ENC", -1, false, false, 0.0f};
  }
  return {};
}

void pressAction(const TouchAction& action) {
  activeAction = action;
  touchActive = action.key != nullptr || action.joystick || action.modifier != 0;
  if (!touchActive) return;

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
  drawScreen();
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
  M5.Display.setRotation(1);
  M5.Display.setBrightness(120);
  M5.Display.setTextWrap(false);

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
    pressAction(actionAt(touch.x, touch.y));
  }
  if (touch.wasReleased()) {
    releaseAction();
  }

  if (kHasBottomButtons) {
    if (M5.BtnA.wasPressed()) {
      page = Page::Tasks;
      drawScreen();
    } else if (M5.BtnB.wasPressed()) {
      page = Page::Commands;
      drawScreen();
    } else if (M5.BtnC.wasPressed()) {
      page = Page::Navigate;
      drawScreen();
    }
  }

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
    if (animated) drawScreen();
  }

  updateBattery();
  delay(8);
}
