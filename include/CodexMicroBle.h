// SPDX-License-Identifier: MIT
// Copyright (c) 2026 imliubo

#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <BLECharacteristic.h>
#include <BLEHIDDevice.h>
#include <BLEServer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include <array>

struct ThreadLight {
  uint32_t color = 0;
  float brightness = 0.0f;
  String effect = "off";
  float speed = 0.0f;
};

struct LightingSide {
  uint32_t color = 0;
  float brightness = 0.0f;
  String effect = "off";
  float speed = 0.0f;
};

struct CodexMicroState {
  std::array<ThreadLight, 6> threads;
  LightingSide ambient;
  LightingSide keys;
  bool connected = false;
  bool dirty = true;
};

class CodexMicroBle {
 public:
  static constexpr uint16_t kVendorId = 0x303A;
  static constexpr uint16_t kProductId = 0x8360;
  static constexpr uint8_t kReportId = 6;
  static constexpr uint8_t kKeyboardReportId = 1;

  void begin();
  void setBattery(uint8_t percentage, bool charging);
  void sendKey(const char* key, uint8_t action, int8_t agent = -1);
  void sendJoystick(float angle, float distance);
  void sendEnter();
  void sendRightAlt();
  // Must be called from the Arduino task; host requests are handled there.
  void poll();
  bool connected();
  CodexMicroState snapshot();

 private:
  class ServerCallbacks;
  class OutputCallbacks;

  struct OutputReport {
    uint8_t length = 0;
    uint8_t data[64] = {};
  };

  void onConnected(bool connected);
  void queueOutput(const uint8_t* data, size_t length);
  void onOutput(const uint8_t* data, size_t length);
  void handleRpc(const JsonDocument& request);
  void sendResult(JsonVariantConst id, JsonVariantConst result);
  void sendSuccess(JsonVariantConst id);
  void sendJson(const String& json);
  void sendKeyboard(uint8_t modifier, uint8_t key);
  void updateThreadLighting(JsonArrayConst values);
  void updateLightingSide(LightingSide& side, JsonObjectConst value);

  BLEHIDDevice* hid_ = nullptr;
  BLECharacteristic* input_ = nullptr;
  BLECharacteristic* output_ = nullptr;
  BLECharacteristic* keyboardInput_ = nullptr;
  SemaphoreHandle_t stateMutex_ = nullptr;
  QueueHandle_t outputQueue_ = nullptr;
  CodexMicroState state_;
  String rpcBuffer_;
  uint8_t batteryPercentage_ = 100;
  bool charging_ = false;
};
