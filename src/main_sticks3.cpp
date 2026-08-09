// SPDX-License-Identifier: MIT
// Copyright (c) 2026 imliubo

#include <Arduino.h>
#include <M5Unified.h>

#include <cmath>

#include "CodexMicroBle.h"
#include "StickS3AgentStatus.h"
#include "StickS3ButtonController.h"
#include "StickS3ScreenFlash.h"

#if !defined(CODEX_MICRO_STICKS3)
#error "main_sticks3.cpp is only for the m5stack-sticks3 environment"
#endif

namespace {

constexpr uint16_t kBackground = 0x0841;
constexpr uint16_t kPanel = 0x18E3;
constexpr uint16_t kSelectedPanel = 0x31A6;
constexpr uint16_t kText = 0xFFFF;
constexpr uint16_t kMuted = 0x9CF3;
constexpr uint16_t kAccent = 0x2E73;
constexpr uint16_t kDisconnected = 0xF800;
constexpr uint16_t kConnected = 0x07E0;
constexpr uint16_t kFlashColor = 0xFFFF;
constexpr int kHeaderHeight = 28;
constexpr int kFooterHeight = 26;
constexpr int kAgentCount = 6;

const char* kAgentKeys[kAgentCount] = {"AG00", "AG01", "AG02",
                                      "AG03", "AG04", "AG05"};

CodexMicroBle codex;
CodexMicroState state;
M5Canvas canvas(&M5.Display);
codex_micro::StickS3ButtonController buttons(kAgentCount);
codex_micro::StickS3ScreenFlash screenFlash;
uint32_t lastDrawMs = 0;
uint32_t lastBatteryMs = 0;

uint16_t rgb888To565(uint32_t color, float brightness = 1.0f) {
  brightness = constrain(brightness, 0.0f, 1.0f);
  const uint8_t red = ((color >> 16) & 0xFF) * brightness;
  const uint8_t green = ((color >> 8) & 0xFF) * brightness;
  const uint8_t blue = (color & 0xFF) * brightness;
  return canvas.color565(red, green, blue);
}

void drawScreen() {
  if (screenFlash.visible()) {
    canvas.fillScreen(kFlashColor);
    canvas.pushSprite(0, 0);
    lastDrawMs = millis();
    return;
  }

  const int width = canvas.width();
  const int height = canvas.height();
  const int contentHeight = height - kHeaderHeight - kFooterHeight;
  const int rowHeight = contentHeight / kAgentCount;

  canvas.fillScreen(kBackground);
  canvas.setTextWrap(false);

  canvas.setTextDatum(middle_left);
  canvas.setTextSize(1);
  canvas.setTextColor(kText);
  canvas.drawString("CODEX TASKS", 5, kHeaderHeight / 2);
  canvas.fillCircle(width - 8, kHeaderHeight / 2, 4,
                    state.connected ? kConnected : kDisconnected);

  for (int i = 0; i < kAgentCount; ++i) {
    const int top = kHeaderHeight + i * rowHeight;
    const bool selected = i == buttons.selectedAgent();
    const ThreadLight& light = state.threads[i];
    float pulse = 1.0f;
    if (light.effect == "breath") {
      pulse = 0.55f +
              0.45f * (std::sin(millis() * 0.006f) * 0.5f + 0.5f);
    }
    const uint16_t statusColor =
        light.brightness <= 0.01f
            ? 0x4208
            : rgb888To565(light.color, light.brightness * pulse);

    canvas.fillRoundRect(3, top + 1, width - 6, rowHeight - 2, 4,
                         selected ? kSelectedPanel : kPanel);
    canvas.drawRoundRect(3, top + 1, width - 6, rowHeight - 2, 4,
                         selected ? kAccent : statusColor);
    canvas.fillCircle(12, top + rowHeight / 2, 4, statusColor);

    char label[12];
    snprintf(label, sizeof(label), "AGENT %d", i + 1);
    canvas.setTextDatum(middle_left);
    canvas.setTextColor(kText);
    canvas.drawString(label, 22, top + rowHeight / 2);

    canvas.setTextDatum(middle_right);
    canvas.setTextColor(light.brightness <= 0.01f ? kMuted : statusColor);
    canvas.drawString(selected ? ">" : "", width - 9,
                      top + rowHeight / 2);
  }

  canvas.fillRect(0, height - kFooterHeight, width, kFooterHeight, kBackground);
  canvas.setTextDatum(middle_center);
  canvas.setTextColor(kMuted);
  canvas.drawString("B:NXT 2X:PRV H:OPEN", width / 2, height - 18);
  canvas.drawString("A:ENTER 2X:ALT", width / 2, height - 7);

  canvas.pushSprite(0, 0);
  lastDrawMs = millis();
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

bool hasBreathingAgent() {
  for (const ThreadLight& light : state.threads) {
    if (light.effect == "breath") {
      return true;
    }
  }
  return false;
}

void handleButtonAction(codex_micro::StickS3ButtonAction action) {
  switch (action) {
    case codex_micro::StickS3ButtonAction::SelectionChanged:
      drawScreen();
      break;
    case codex_micro::StickS3ButtonAction::ActivateSelectedAgent: {
      const int agent = buttons.selectedAgent();
      codex.sendKey(kAgentKeys[agent], 1, agent);
      delay(8);
      codex.sendKey(kAgentKeys[agent], 0, agent);
      break;
    }
    case codex_micro::StickS3ButtonAction::SendEnter:
      codex.sendEnter();
      break;
    case codex_micro::StickS3ButtonAction::SendRightAlt:
      codex.sendRightAlt();
      break;
    case codex_micro::StickS3ButtonAction::None:
      break;
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(600);
  Serial.println("Codex Micro StickS3 boot");

  auto config = M5.config();
  config.clear_display = true;
  M5.begin(config);
  M5.Display.setRotation(0);
  M5.Display.setBrightness(120);
  M5.Display.setTextWrap(false);

  canvas.setColorDepth(16);
  if (canvas.createSprite(M5.Display.width(), M5.Display.height()) == nullptr) {
    M5.Display.fillScreen(TFT_BLACK);
    M5.Display.setTextColor(TFT_RED);
    M5.Display.setTextDatum(middle_center);
    M5.Display.drawString("Canvas failed", M5.Display.width() / 2,
                          M5.Display.height() / 2);
    Serial.println("Canvas allocation failed");
    while (true) {
      delay(1000);
    }
  }

  codex.begin();
  state = codex.snapshot();
  updateBattery();
  drawScreen();
  Serial.println("CODEX_MICRO_READY");
}

void loop() {
  M5.update();
  codex.poll();

  if (M5.BtnB.wasHold()) {
    handleButtonAction(buttons.onButtonBHold());
  }

  if (M5.BtnB.wasReleased()) {
    handleButtonAction(
        buttons.onButtonBReleased(millis(), M5.BtnB.wasReleasedAfterHold()));
  }

  if (M5.BtnA.wasReleased()) {
    handleButtonAction(buttons.onButtonAReleased(millis()));
  }

  const uint32_t now = millis();
  handleButtonAction(buttons.pollButtonA(now));
  handleButtonAction(buttons.pollButtonB(now, M5.BtnB.isPressed()));

  CodexMicroState latest = codex.snapshot();
  const bool statusChanged =
      codex_micro::agentStatusesChanged(state.threads, latest.threads);
  const bool stateNeedsRedraw =
      latest.dirty || latest.connected != state.connected;
  state = latest;
  if (statusChanged) {
    screenFlash.start(now);
  }
  if (stateNeedsRedraw || statusChanged) {
    drawScreen();
  }

  const uint32_t animationNow = millis();
  if (screenFlash.update(animationNow)) {
    drawScreen();
  }

  if (hasBreathingAgent() && animationNow - lastDrawMs > 80) {
    drawScreen();
  }

  updateBattery();
  delay(8);
}
