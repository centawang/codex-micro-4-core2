// SPDX-License-Identifier: MIT
// Copyright (c) 2026 imliubo

#include <Arduino.h>
#include <M5Unified.h>
#include <Preferences.h>

#include <cmath>

#include "CodexMicroBle.h"
#include "CoreAgentCardStyle.h"
#include "StickS3AgentStatus.h"
#include "StickS3ButtonConfig.h"
#include "StickS3ButtonController.h"
#include "StickS3PowerController.h"
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
constexpr uint16_t kSelectedIndicator = 0xF800;
constexpr uint16_t kDisconnected = 0xF800;
constexpr uint16_t kConnected = 0x07E0;
constexpr uint16_t kFlashColor = 0xFFFF;
constexpr uint8_t kNormalBrightness = 120;
constexpr uint8_t kDimBrightness = 20;
constexpr int kHeaderHeight = 28;
constexpr int kFooterHeight = 26;
constexpr int kAgentCount = 6;
constexpr size_t kButtonSlotCount =
  static_cast<size_t>(codex_micro::StickS3ButtonSlot::Count);
constexpr uint32_t kDefaultDimAfterSeconds = 60;
constexpr uint32_t kDefaultPowerOffAfterSeconds = 30 * 60;
constexpr uint32_t kMaximumPowerTimeoutSeconds = 7 * 24 * 60 * 60;

const char* kAgentKeys[kAgentCount] = {"AG00", "AG01", "AG02",
                                      "AG03", "AG04", "AG05"};
const char* kButtonPreferenceKeys[kButtonSlotCount] = {
  "a_single", "a_double", "b_single", "b_double", "b_hold"};
const char* kDefaultButtonActions[kButtonSlotCount] = {
  "enter", "right_alt", "next", "previous", "activate"};
constexpr char kButtonPreferencesNamespace[] = "stick-buttons";
constexpr char kPowerPreferencesNamespace[] = "stick-power";
constexpr char kDimAfterPreferenceKey[] = "dim_sec";
constexpr char kPowerOffAfterPreferenceKey[] = "off_sec";

CodexMicroBle codex;
CodexMicroState state;
M5Canvas canvas(&M5.Display);
codex_micro::StickS3ButtonController buttons(kAgentCount);
codex_micro::StickS3PowerController powerController;
codex_micro::StickS3ScreenFlash screenFlash;
uint32_t lastDrawMs = 0;
uint32_t lastBatteryMs = 0;
String buttonActionTokens[kButtonSlotCount];
String buttonConfigInput;
uint32_t dimAfterSeconds = kDefaultDimAfterSeconds;
uint32_t powerOffAfterSeconds = kDefaultPowerOffAfterSeconds;

void handlePowerAction(codex_micro::StickS3PowerAction action);

uint16_t rgb888To565(uint32_t color, float brightness = 1.0f) {
  brightness = constrain(brightness, 0.0f, 1.0f);
  const uint8_t red = ((color >> 16) & 0xFF) * brightness;
  const uint8_t green = ((color >> 8) & 0xFF) * brightness;
  const uint8_t blue = (color & 0xFF) * brightness;
  return canvas.color565(red, green, blue);
}

void applyButtonActionToken(size_t index, const String& token) {
  codex_micro::StickS3ButtonAction action;
  if (!codex_micro::parseStickS3ButtonAction(token.c_str(), action)) {
    return;
  }
  buttonActionTokens[index] = token;
  buttons.setAction(static_cast<codex_micro::StickS3ButtonSlot>(index), action);
}

void loadButtonConfig() {
  Preferences preferences;
  const bool opened = preferences.begin(kButtonPreferencesNamespace, true);
  for (size_t index = 0; index < kButtonSlotCount; ++index) {
    String token = opened
                       ? preferences.getString(kButtonPreferenceKeys[index],
                                               kDefaultButtonActions[index])
                       : String(kDefaultButtonActions[index]);
    codex_micro::StickS3ButtonAction action;
    if (!codex_micro::parseStickS3ButtonAction(token.c_str(), action)) {
      token = kDefaultButtonActions[index];
    }
    applyButtonActionToken(index, token);
  }
  if (opened) {
    preferences.end();
  }
}

void applyPowerConfig(uint32_t dimSeconds, uint32_t powerOffSeconds,
                      uint32_t now) {
  dimAfterSeconds = dimSeconds;
  powerOffAfterSeconds = powerOffSeconds;
  handlePowerAction(powerController.setTimeouts(dimSeconds * 1000U,
                                                powerOffSeconds * 1000U, now));
}

void loadPowerConfig() {
  Preferences preferences;
  const bool opened = preferences.begin(kPowerPreferencesNamespace, true);
  uint32_t dimSeconds =
      opened ? preferences.getUInt(kDimAfterPreferenceKey,
                                   kDefaultDimAfterSeconds)
             : kDefaultDimAfterSeconds;
  uint32_t powerOffSeconds =
      opened ? preferences.getUInt(kPowerOffAfterPreferenceKey,
                                   kDefaultPowerOffAfterSeconds)
             : kDefaultPowerOffAfterSeconds;
  if (opened) {
    preferences.end();
  }
  if (dimSeconds > kMaximumPowerTimeoutSeconds) {
    dimSeconds = kDefaultDimAfterSeconds;
  }
  if (powerOffSeconds > kMaximumPowerTimeoutSeconds) {
    powerOffSeconds = kDefaultPowerOffAfterSeconds;
  }
  applyPowerConfig(dimSeconds, powerOffSeconds, 0);
}

bool saveButtonConfig(const String* tokens) {
  Preferences preferences;
  if (!preferences.begin(kButtonPreferencesNamespace, false)) {
    return false;
  }
  bool saved = true;
  for (size_t index = 0; index < kButtonSlotCount; ++index) {
    if (preferences.putString(kButtonPreferenceKeys[index], tokens[index]) ==
        0) {
      saved = false;
    }
  }
  preferences.end();
  return saved;
}

bool savePowerConfig(uint32_t dimSeconds, uint32_t powerOffSeconds) {
  Preferences preferences;
  if (!preferences.begin(kPowerPreferencesNamespace, false)) {
    return false;
  }
  const bool saved =
      preferences.putUInt(kDimAfterPreferenceKey, dimSeconds) ==
          sizeof(dimSeconds) &&
      preferences.putUInt(kPowerOffAfterPreferenceKey, powerOffSeconds) ==
          sizeof(powerOffSeconds);
  preferences.end();
  return saved;
}

void printButtonConfig() {
  Serial.print("BUTTONS CONFIG");
  for (size_t index = 0; index < kButtonSlotCount; ++index) {
    Serial.print(' ');
    Serial.print(buttonActionTokens[index]);
  }
  Serial.println();
}

void printPowerConfig() {
  Serial.printf("POWER CONFIG %lu %lu\n",
                static_cast<unsigned long>(dimAfterSeconds),
                static_cast<unsigned long>(powerOffAfterSeconds));
}

bool parsePowerTimeout(const String& token, uint32_t& value) {
  if (token.isEmpty()) {
    return false;
  }
  uint32_t parsed = 0;
  for (size_t index = 0; index < token.length(); ++index) {
    const char character = token[index];
    if (character < '0' || character > '9') {
      return false;
    }
    parsed = parsed * 10U + static_cast<uint32_t>(character - '0');
    if (parsed > kMaximumPowerTimeoutSeconds) {
      return false;
    }
  }
  value = parsed;
  return true;
}

bool parsePowerConfigPayload(String payload, uint32_t& dimSeconds,
                             uint32_t& powerOffSeconds) {
  payload.trim();
  const int separator = payload.indexOf(' ');
  if (separator <= 0 || payload.indexOf(' ', separator + 1) >= 0) {
    return false;
  }
  return parsePowerTimeout(payload.substring(0, separator), dimSeconds) &&
         parsePowerTimeout(payload.substring(separator + 1), powerOffSeconds);
}

bool parseButtonConfigPayload(const String& payload, String* tokens) {
  int cursor = 0;
  for (size_t index = 0; index < kButtonSlotCount; ++index) {
    while (cursor < static_cast<int>(payload.length()) &&
           payload[cursor] == ' ') {
      ++cursor;
    }
    if (cursor >= static_cast<int>(payload.length())) {
      return false;
    }
    int end = payload.indexOf(' ', cursor);
    if (end < 0) {
      end = payload.length();
    }
    tokens[index] = payload.substring(cursor, end);
    codex_micro::StickS3ButtonAction action;
    if (!codex_micro::parseStickS3ButtonAction(tokens[index].c_str(), action)) {
      return false;
    }
    cursor = end;
  }
  while (cursor < static_cast<int>(payload.length()) &&
         payload[cursor] == ' ') {
    ++cursor;
  }
  return cursor == static_cast<int>(payload.length());
}

void processButtonConfigCommand(String command) {
  command.trim();
  if (command == "POWER GET" || command == "POWER PING") {
    Serial.println("POWER READY 1");
    printPowerConfig();
    return;
  }
  if (command == "POWER RESET") {
    if (!savePowerConfig(kDefaultDimAfterSeconds,
                         kDefaultPowerOffAfterSeconds)) {
      Serial.println("POWER ERROR save-failed");
      return;
    }
    applyPowerConfig(kDefaultDimAfterSeconds, kDefaultPowerOffAfterSeconds,
                     millis());
    Serial.println("POWER OK");
    printPowerConfig();
    return;
  }
  constexpr char kPowerSetPrefix[] = "POWER SET ";
  if (command.startsWith(kPowerSetPrefix)) {
    uint32_t dimSeconds = 0;
    uint32_t powerOffSeconds = 0;
    if (!parsePowerConfigPayload(
            command.substring(strlen(kPowerSetPrefix)), dimSeconds,
            powerOffSeconds)) {
      Serial.println("POWER ERROR invalid-config");
      return;
    }
    if (!savePowerConfig(dimSeconds, powerOffSeconds)) {
      Serial.println("POWER ERROR save-failed");
      return;
    }
    applyPowerConfig(dimSeconds, powerOffSeconds, millis());
    Serial.println("POWER OK");
    printPowerConfig();
    return;
  }
  if (command == "BUTTONS GET" || command == "BUTTONS PING") {
    Serial.println("BUTTONS READY 1");
    printButtonConfig();
    return;
  }
  if (command == "BUTTONS RESET") {
    String tokens[kButtonSlotCount];
    for (size_t index = 0; index < kButtonSlotCount; ++index) {
      tokens[index] = kDefaultButtonActions[index];
    }
    if (!saveButtonConfig(tokens)) {
      Serial.println("BUTTONS ERROR save-failed");
      return;
    }
    for (size_t index = 0; index < kButtonSlotCount; ++index) {
      applyButtonActionToken(index, tokens[index]);
    }
    Serial.println("BUTTONS OK");
    printButtonConfig();
    return;
  }
  constexpr char kSetPrefix[] = "BUTTONS SET ";
  if (command.startsWith(kSetPrefix)) {
    String tokens[kButtonSlotCount];
    if (!parseButtonConfigPayload(command.substring(strlen(kSetPrefix)),
                                  tokens)) {
      Serial.println("BUTTONS ERROR invalid-config");
      return;
    }
    if (!saveButtonConfig(tokens)) {
      Serial.println("BUTTONS ERROR save-failed");
      return;
    }
    for (size_t index = 0; index < kButtonSlotCount; ++index) {
      applyButtonActionToken(index, tokens[index]);
    }
    Serial.println("BUTTONS OK");
    printButtonConfig();
    return;
  }
  if (command.startsWith("BUTTONS ")) {
    Serial.println("BUTTONS ERROR unknown-command");
  } else if (command.startsWith("POWER ")) {
    Serial.println("POWER ERROR unknown-command");
  }
}

void pollButtonConfigSerial() {
  while (Serial.available() > 0) {
    const int value = Serial.read();
    if (value < 0) {
      return;
    }
    const char character = static_cast<char>(value);
    if (character == '\n') {
      processButtonConfigCommand(buttonConfigInput);
      buttonConfigInput.clear();
    } else if (character != '\r' && character >= 0x20 && character <= 0x7E) {
      if (buttonConfigInput.length() < 240) {
        buttonConfigInput += character;
      } else {
        buttonConfigInput.clear();
        Serial.println("BUTTONS ERROR command-too-long");
      }
    }
  }
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
    const bool assigned = light.brightness > 0.01f;
    float pulse = 1.0f;
    if (light.effect == "breath") {
      pulse = 0.55f +
              0.45f * (std::sin(millis() * 0.006f) * 0.5f + 0.5f);
    }
    const uint16_t statusColor =
        !assigned
            ? 0x4208
            : rgb888To565(light.color, light.brightness * pulse);
    const uint16_t fill = codex_micro::agentCardFill(
        statusColor, assigned, false, selected ? kSelectedPanel : kPanel);
    const uint16_t textColor =
        codex_micro::agentCardTextColor(fill, kText, kBackground);

    canvas.fillRoundRect(3, top + 1, width - 6, rowHeight - 2, 4, fill);
    canvas.drawRoundRect(3, top + 1, width - 6, rowHeight - 2, 4,
                         selected ? kAccent : statusColor);
    canvas.fillCircle(12, top + rowHeight / 2, 4, statusColor);

    char label[12];
    snprintf(label, sizeof(label), "AGENT %d", i + 1);
    canvas.setTextDatum(middle_left);
    canvas.setTextColor(textColor);
    canvas.drawString(label, 22, top + rowHeight / 2);

    if (selected) {
      canvas.fillCircle(width - 12, top + rowHeight / 2, 4,
                        kSelectedIndicator);
    }
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
  switch (action.kind) {
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
    case codex_micro::StickS3ButtonAction::KeyboardShortcut:
      codex.sendKeyboardShortcut(action.modifier, action.key);
      break;
    case codex_micro::StickS3ButtonAction::CodexCommand: {
      const char* key =
          codex_micro::stickS3CodexCommandKey(action.commandIndex);
      if (key != nullptr) {
        codex.sendKey(key, 1);
        delay(8);
        codex.sendKey(key, 0);
      }
      break;
    }
    case codex_micro::StickS3ButtonAction::None:
      break;
  }
}

void handlePowerAction(codex_micro::StickS3PowerAction action) {
  switch (action) {
    case codex_micro::StickS3PowerAction::DimDisplay:
      M5.Display.setBrightness(kDimBrightness);
      break;
    case codex_micro::StickS3PowerAction::RestoreDisplay:
      M5.Display.setBrightness(kNormalBrightness);
      break;
    case codex_micro::StickS3PowerAction::PowerOff:
      Serial.println("Inactivity timeout; powering off");
      M5.Power.powerOff();
      break;
    case codex_micro::StickS3PowerAction::None:
      break;
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(600);
  Serial.println("Codex Micro StickS3 boot");
  loadButtonConfig();
  loadPowerConfig();

  auto config = M5.config();
  config.clear_display = true;
  M5.begin(config);
  M5.Display.setRotation(0);
  M5.Display.setBrightness(kNormalBrightness);
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
  powerController.begin(millis());
  updateBattery();
  drawScreen();
  Serial.println("CODEX_MICRO_READY");
  Serial.println("BUTTONS READY 1");
  printButtonConfig();
  Serial.println("POWER READY 1");
  printPowerConfig();
}

void loop() {
  M5.update();
  codex.poll();
  pollButtonConfigSerial();

  const uint32_t inputNow = millis();
  if (M5.BtnA.wasPressed() || M5.BtnB.wasPressed()) {
    handlePowerAction(powerController.onActivity(inputNow));
  }

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
    handlePowerAction(powerController.onActivity(now));
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
  handlePowerAction(powerController.update(millis()));
  delay(8);
}
