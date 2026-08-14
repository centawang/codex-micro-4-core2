#pragma once

#include <math.h>
#include <stdint.h>

namespace codex_micro {

constexpr float kStopWatchPi = 3.14159265358979323846f;
constexpr float kStopWatchTwoPi = 2.0f * kStopWatchPi;

struct StopWatchJoystickPosition {
  float angle = 0.0f;
  float distance = 0.0f;
};

inline bool stopWatchPointInCircle(float x, float y, float centerX,
                                   float centerY, float radius) {
  const float deltaX = x - centerX;
  const float deltaY = y - centerY;
  return radius > 0.0f &&
         deltaX * deltaX + deltaY * deltaY <= radius * radius;
}

inline StopWatchJoystickPosition stopWatchJoystickPosition(
    float x, float y, float centerX, float centerY, float radius) {
  StopWatchJoystickPosition position;
  const float deltaX = x - centerX;
  const float deltaY = y - centerY;
  const float magnitude = sqrtf(deltaX * deltaX + deltaY * deltaY);
  if (magnitude > 0.0f) {
    position.angle = atan2f(deltaY, deltaX) / kStopWatchTwoPi;
    if (position.angle < 0.0f) {
      position.angle += 1.0f;
    }
  }
  if (radius > 0.0f) {
    position.distance = magnitude >= radius ? 1.0f : magnitude / radius;
  }
  return position;
}

class StopWatchDialGesture {
 public:
  explicit StopWatchDialGesture(uint8_t stepsPerTurn = 24)
      : stepRadians_(kStopWatchTwoPi /
                     static_cast<float>(stepsPerTurn == 0 ? 1
                                                           : stepsPerTurn)) {}

  void begin(float x, float y, float centerX, float centerY) {
    previousAngle_ = atan2f(y - centerY, x - centerX);
    accumulatedRadians_ = 0.0f;
    active_ = true;
  }

  int update(float x, float y, float centerX, float centerY) {
    if (!active_) {
      return 0;
    }
    const float angle = atan2f(y - centerY, x - centerX);
    float delta = angle - previousAngle_;
    if (delta > kStopWatchPi) {
      delta -= kStopWatchTwoPi;
    } else if (delta < -kStopWatchPi) {
      delta += kStopWatchTwoPi;
    }
    previousAngle_ = angle;
    accumulatedRadians_ += delta;

    int steps = 0;
    while (accumulatedRadians_ >= stepRadians_) {
      accumulatedRadians_ -= stepRadians_;
      ++steps;
    }
    while (accumulatedRadians_ <= -stepRadians_) {
      accumulatedRadians_ += stepRadians_;
      --steps;
    }
    return steps;
  }

  void reset() {
    active_ = false;
    accumulatedRadians_ = 0.0f;
  }

 private:
  float stepRadians_;
  float previousAngle_ = 0.0f;
  float accumulatedRadians_ = 0.0f;
  bool active_ = false;
};

}  // namespace codex_micro