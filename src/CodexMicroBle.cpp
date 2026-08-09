// SPDX-License-Identifier: MIT
// Copyright (c) 2026 imliubo

#include "CodexMicroBle.h"

#include <BLE2902.h>
#include <BLEDevice.h>
#include <BLESecurity.h>
#include <BLEUtils.h>

namespace {

constexpr char kDeviceName[] = "Codex Micro";
constexpr char kManufacturer[] = "Work Louder";
#if defined(CODEX_MICRO_STICKS3)
constexpr char kFirmwareVersion[] = "0.1.0-sticks3";
#elif defined(ARDUINO_M5STACK_CORES3)
constexpr char kFirmwareVersion[] = "0.1.0-cores3";
#else
constexpr char kFirmwareVersion[] = "0.1.0-core2";
#endif
constexpr size_t kPayloadSize = 61;
constexpr size_t kReportBodySize = 63;
constexpr size_t kKeyboardReportSize = 8;
constexpr uint8_t kRightAltModifier = 0x40;
constexpr uint8_t kEnterKey = 0x28;
// Notifications are capped at MTU - 3, and the library defaults the local MTU
// to 23, which would truncate every report body.
constexpr uint16_t kAttMtu = 185;

constexpr uint16_t swapBytes(uint16_t value) {
  return static_cast<uint16_t>((value << 8) | (value >> 8));
}

// StickS3 additionally exposes a standard keyboard input report so BtnA can
// emit Right Alt without changing the vendor protocol. HIDAPI adds/removes
// report IDs, while the BLE characteristics carry only each report body.
const uint8_t kReportMap[] = {
#if defined(CODEX_MICRO_STICKS3)
    0x05, 0x01,              // Usage Page (Generic Desktop)
    0x09, 0x06,              // Usage (Keyboard)
    0xA1, 0x01,              // Collection (Application)
    0x85, 0x01,              // Report ID (1)
    0x05, 0x07,              // Usage Page (Keyboard/Keypad)
    0x19, 0xE0,              // Usage Minimum (Left Control)
    0x29, 0xE7,              // Usage Maximum (Right GUI)
    0x15, 0x00,              // Logical Minimum (0)
    0x25, 0x01,              // Logical Maximum (1)
    0x75, 0x01,              // Report Size (1)
    0x95, 0x08,              // Report Count (8 modifiers)
    0x81, 0x02,              // Input (Data, Variable, Absolute)
    0x95, 0x01,              // Report Count (1)
    0x75, 0x08,              // Report Size (8)
    0x81, 0x01,              // Input (Constant, Array, Absolute)
    0x95, 0x06,              // Report Count (6 keys)
    0x75, 0x08,              // Report Size (8)
    0x15, 0x00,              // Logical Minimum (0)
    0x25, 0x65,              // Logical Maximum (101)
    0x05, 0x07,              // Usage Page (Keyboard/Keypad)
    0x19, 0x00,              // Usage Minimum (Reserved)
    0x29, 0x65,              // Usage Maximum (Keyboard Application)
    0x81, 0x00,              // Input (Data, Array, Absolute)
    0xC0,                    // End Collection
#endif
    0x06, 0x00, 0xFF,        // Usage Page (Vendor Defined 0xFF00)
    0x09, 0x01,              // Usage (1)
    0xA1, 0x01,              // Collection (Application)
    0x85, 0x06,              // Report ID (6)
    0x15, 0x00,              // Logical Minimum (0)
    0x26, 0xFF, 0x00,        // Logical Maximum (255)
    0x75, 0x08,              // Report Size (8)
    0x95, 0x3F,              // Report Count (63)
    0x09, 0x01,              // Usage (1)
    0x81, 0x02,              // Input (Data, Variable, Absolute)
    0x95, 0x3F,              // Report Count (63)
    0x09, 0x02,              // Usage (2)
    0x91, 0x02,              // Output (Data, Variable, Absolute)
    0xC0                     // End Collection
};

class SecurityCallbacks final : public BLESecurityCallbacks {
 public:
  bool onSecurityRequest() override { return true; }
  uint32_t onPassKeyRequest() override { return 0; }
  void onPassKeyNotify(uint32_t) override {}
  bool onConfirmPIN(uint32_t) override { return true; }
  void onAuthenticationComplete(esp_ble_auth_cmpl_t result) override {
    Serial.printf("BLE pairing %s\n", result.success ? "complete" : "failed");
  }
};

class InputStatusCallbacks final : public BLECharacteristicCallbacks {
 public:
  void onStatus(BLECharacteristic*, Status status, uint32_t code) override {
    if (status != Status::SUCCESS_NOTIFY) {
      Serial.printf("Notify failed status=%d code=%u\n", static_cast<int>(status),
                    code);
    }
  }
};

}  // namespace

class CodexMicroBle::ServerCallbacks final : public BLEServerCallbacks {
 public:
  explicit ServerCallbacks(CodexMicroBle& owner) : owner_(owner) {}

  void onConnect(BLEServer*) override { owner_.onConnected(true); }

  void onDisconnect(BLEServer*) override {
    owner_.onConnected(false);
    BLEDevice::startAdvertising();
  }

 private:
  CodexMicroBle& owner_;
};

class CodexMicroBle::OutputCallbacks final : public BLECharacteristicCallbacks {
 public:
  explicit OutputCallbacks(CodexMicroBle& owner) : owner_(owner) {}

  void onWrite(BLECharacteristic* characteristic) override {
    const std::string value = characteristic->getValue();
    owner_.queueOutput(reinterpret_cast<const uint8_t*>(value.data()), value.size());
  }

 private:
  CodexMicroBle& owner_;
};

void CodexMicroBle::begin() {
  stateMutex_ = xSemaphoreCreateMutex();
  outputQueue_ = xQueueCreate(8, sizeof(OutputReport));

  BLEDevice::init(kDeviceName);
  BLEDevice::setMTU(kAttMtu);
  BLEDevice::setSecurityCallbacks(new SecurityCallbacks());

  auto* security = new BLESecurity();
  security->setCapability(ESP_IO_CAP_NONE);
  security->setAuthenticationMode(ESP_LE_AUTH_BOND);

  BLEServer* server = BLEDevice::createServer();
  server->setCallbacks(new ServerCallbacks(*this));

  hid_ = new BLEHIDDevice(server);
  hid_->manufacturer()->setValue(kManufacturer);
  // Low release bits mark the transport as wireless in the desktop bridge.
  // Arduino-ESP32 2.x serializes these fields big-endian, while the BLE PnP
  // characteristic is little-endian. Pre-swap so macOS enumerates 303A:8360.
  hid_->pnp(0x02, swapBytes(kVendorId), swapBytes(kProductId), swapBytes(0x0101));
  hid_->hidInfo(0x00, 0x01);
  hid_->reportMap(const_cast<uint8_t*>(kReportMap), sizeof(kReportMap));

  input_ = hid_->inputReport(kReportId);
  input_->setCallbacks(new InputStatusCallbacks());
  output_ = hid_->outputReport(kReportId);
  output_->setCallbacks(new OutputCallbacks(*this));
#if defined(CODEX_MICRO_STICKS3)
  keyboardInput_ = hid_->inputReport(kKeyboardReportId);
  keyboardInput_->setCallbacks(new InputStatusCallbacks());
#endif
  hid_->startServices();
  hid_->setBatteryLevel(batteryPercentage_);

  BLEAdvertising* advertising = BLEDevice::getAdvertising();
  advertising->setAppearance(GENERIC_HID);
  advertising->addServiceUUID(hid_->hidService()->getUUID());
  advertising->setScanResponse(true);
  advertising->setMinPreferred(0x06);
  advertising->setMaxPreferred(0x12);
  BLEDevice::startAdvertising();

  Serial.printf(
      "BLE vendor HID ready VID=%04X PID=%04X usage=FF00 report=%u\n",
      kVendorId, kProductId, kReportId);
}

void CodexMicroBle::setBattery(uint8_t percentage, bool charging) {
  batteryPercentage_ = constrain(percentage, 0, 100);
  charging_ = charging;
  if (hid_ != nullptr && connected()) {
    hid_->setBatteryLevel(batteryPercentage_);
  }
}

void CodexMicroBle::sendKey(const char* key, uint8_t action, int8_t agent) {
  StaticJsonDocument<192> message;
  message["method"] = "v.oai.hid";
  JsonObject params = message.createNestedObject("params");
  params["k"] = key;
  params["act"] = action;
  if (agent >= 0) {
    params["ag"] = agent;
  }

  String json;
  serializeJson(message, json);
  sendJson(json);
  Serial.printf("HID key=%s action=%u\n", key, action);
}

void CodexMicroBle::sendJoystick(float angle, float distance) {
  StaticJsonDocument<160> message;
  message["method"] = "v.oai.rad";
  JsonObject params = message.createNestedObject("params");
  params["a"] = angle;
  params["d"] = distance;

  String json;
  serializeJson(message, json);
  sendJson(json);
}

void CodexMicroBle::sendEnter() {
  sendKeyboard(0, kEnterKey);
  Serial.println("Keyboard Enter");
}

void CodexMicroBle::sendRightAlt() {
  sendKeyboard(kRightAltModifier, 0);
  Serial.println("Keyboard Right Alt");
}

void CodexMicroBle::sendKeyboard(uint8_t modifier, uint8_t key) {
  if (keyboardInput_ == nullptr || !connected()) {
    return;
  }

  uint8_t report[kKeyboardReportSize] = {};
  report[0] = modifier;
  report[2] = key;
  keyboardInput_->setValue(report, sizeof(report));
  keyboardInput_->notify();
  delay(8);

  memset(report, 0, sizeof(report));
  keyboardInput_->setValue(report, sizeof(report));
  keyboardInput_->notify();
}

bool CodexMicroBle::connected() {
  if (stateMutex_ == nullptr) {
    return false;
  }
  xSemaphoreTake(stateMutex_, portMAX_DELAY);
  const bool result = state_.connected;
  xSemaphoreGive(stateMutex_);
  return result;
}

CodexMicroState CodexMicroBle::snapshot() {
  CodexMicroState copy;
  if (stateMutex_ == nullptr) {
    return copy;
  }
  xSemaphoreTake(stateMutex_, portMAX_DELAY);
  copy = state_;
  state_.dirty = false;
  xSemaphoreGive(stateMutex_);
  return copy;
}

void CodexMicroBle::onConnected(bool connected) {
  xSemaphoreTake(stateMutex_, portMAX_DELAY);
  state_.connected = connected;
  state_.dirty = true;
  xSemaphoreGive(stateMutex_);
  rpcBuffer_.clear();
  Serial.printf("BLE host %s\n", connected ? "connected" : "disconnected");
}

// Runs on the Bluedroid callback task, whose stack cannot absorb JSON parsing
// and a reply notification, so only the raw report is captured here.
void CodexMicroBle::queueOutput(const uint8_t* data, size_t length) {
  if (outputQueue_ == nullptr || data == nullptr || length == 0) {
    return;
  }
  OutputReport report;
  report.length = static_cast<uint8_t>(min<size_t>(length, sizeof(report.data)));
  memcpy(report.data, data, report.length);
  xQueueSend(outputQueue_, &report, 0);
}

void CodexMicroBle::poll() {
  if (outputQueue_ == nullptr) {
    return;
  }
  OutputReport report;
  while (xQueueReceive(outputQueue_, &report, 0) == pdTRUE) {
    onOutput(report.data, report.length);
  }
}

void CodexMicroBle::onOutput(const uint8_t* data, size_t length) {
  if (data == nullptr || length < 2) {
    return;
  }

  // HOGP normally strips the report ID. Accept an included ID as well so the
  // transport remains compatible with hosts that forward the raw report.
  size_t offset = (length >= 3 && data[0] == kReportId) ? 1 : 0;
  if (length < offset + 2 || data[offset] != 2) {
    return;
  }

  const size_t payloadLength = min<size_t>(data[offset + 1], kPayloadSize);
  if (length < offset + 2 + payloadLength) {
    return;
  }
  const char* payload = reinterpret_cast<const char*>(data + offset + 2);
  constexpr char kTopLevelPrefix[] = "{\"method\"";
  const bool startsTopLevel =
      payloadLength >= sizeof(kTopLevelPrefix) - 1 &&
      memcmp(payload, kTopLevelPrefix, sizeof(kTopLevelPrefix) - 1) == 0;
  if (startsTopLevel && !rpcBuffer_.isEmpty()) {
    // A new top-level object means a previous fragmented write was dropped.
    // Resynchronize immediately instead of poisoning the next request.
    rpcBuffer_.clear();
  }
  if (rpcBuffer_.isEmpty()) {
    size_t jsonStart = 0;
    while (jsonStart < payloadLength && payload[jsonStart] != '{') {
      ++jsonStart;
    }
    if (jsonStart == payloadLength) {
      return;
    }
    rpcBuffer_.concat(payload + jsonStart, payloadLength - jsonStart);
  } else {
    rpcBuffer_.concat(payload, payloadLength);
  }

  DynamicJsonDocument request(4096);
  const DeserializationError error = deserializeJson(request, rpcBuffer_);
  if (error == DeserializationError::IncompleteInput) {
    return;
  }
  if (error) {
    Serial.printf("RPC parse error: %s\n", error.c_str());
    rpcBuffer_.clear();
    return;
  }

  handleRpc(request);
  rpcBuffer_.clear();
}

void CodexMicroBle::handleRpc(const JsonDocument& request) {
  const char* method = request["method"] | "";
  JsonVariantConst id = request["id"];
  JsonVariantConst params = request["params"];
  Serial.printf("RPC method=%s\n", method);

  if (strcmp(method, "sys.version") == 0) {
    StaticJsonDocument<128> resultDoc;
    resultDoc["version"] = kFirmwareVersion;
    sendResult(id, resultDoc.as<JsonVariantConst>());
    return;
  }

  if (strcmp(method, "device.status") == 0) {
    StaticJsonDocument<256> resultDoc;
    resultDoc["version"] = kFirmwareVersion;
    resultDoc["profile_index"] = 0;
    resultDoc["layer_index"] = 1;
    resultDoc["battery"] = batteryPercentage_;
    resultDoc["is_charging"] = charging_;
    sendResult(id, resultDoc.as<JsonVariantConst>());
    return;
  }

  if (strcmp(method, "v.oai.thstatus") == 0 && params.is<JsonArrayConst>()) {
    updateThreadLighting(params.as<JsonArrayConst>());
    sendSuccess(id);
    return;
  }

  if (strcmp(method, "v.oai.rgbcfg") == 0 && params.is<JsonObjectConst>()) {
    xSemaphoreTake(stateMutex_, portMAX_DELAY);
    JsonObjectConst config = params.as<JsonObjectConst>();
    updateLightingSide(state_.ambient, config["ambient"].as<JsonObjectConst>());
    updateLightingSide(state_.keys, config["keys"].as<JsonObjectConst>());
    state_.dirty = true;
    xSemaphoreGive(stateMutex_);
    sendSuccess(id);
    return;
  }

  if (strcmp(method, "lights.preview") == 0 || strcmp(method, "host.focused_app") == 0) {
    sendSuccess(id);
    return;
  }

  StaticJsonDocument<192> response;
  response["id"] = id;
  JsonObject error = response.createNestedObject("error");
  error["code"] = -32601;
  error["message"] = "Method not found";
  String json;
  serializeJson(response, json);
  sendJson(json);
}

void CodexMicroBle::sendResult(JsonVariantConst id, JsonVariantConst result) {
  DynamicJsonDocument response(512);
  response["id"] = id;
  response["result"] = result;
  String json;
  serializeJson(response, json);
  sendJson(json);
}

void CodexMicroBle::sendSuccess(JsonVariantConst id) {
  StaticJsonDocument<96> resultDoc;
  resultDoc["ok"] = true;
  sendResult(id, resultDoc.as<JsonVariantConst>());
}

void CodexMicroBle::sendJson(const String& json) {
  if (input_ == nullptr) {
    return;
  }
  if (!connected()) {
    return;
  }

  String framed = json;
  framed += '\n';
  size_t offset = 0;
  while (offset < framed.length()) {
    const size_t chunk = min<size_t>(kPayloadSize, framed.length() - offset);
    uint8_t report[kReportBodySize] = {};
    report[0] = 2;
    report[1] = chunk;
    memcpy(report + 2, framed.c_str() + offset, chunk);
    input_->setValue(report, sizeof(report));
    input_->notify();
    offset += chunk;
    delay(4);
  }
}

void CodexMicroBle::updateThreadLighting(JsonArrayConst values) {
  xSemaphoreTake(stateMutex_, portMAX_DELAY);
  for (JsonObjectConst value : values) {
    const int id = value["id"] | -1;
    if (id < 0 || id >= static_cast<int>(state_.threads.size())) {
      continue;
    }
    ThreadLight& light = state_.threads[id];
    light.color = value["c"] | light.color;
    light.brightness = value["b"] | light.brightness;
    light.effect = value["e"] | light.effect;
    light.speed = value["s"] | light.speed;
  }
  state_.dirty = true;
  xSemaphoreGive(stateMutex_);
}

void CodexMicroBle::updateLightingSide(LightingSide& side, JsonObjectConst value) {
  if (value.isNull()) {
    return;
  }
  side.color = value["c"] | side.color;
  side.brightness = value["b"] | side.brightness;
  side.effect = value["e"] | side.effect;
  side.speed = value["s"] | side.speed;
}
