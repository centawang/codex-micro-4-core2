// SPDX-License-Identifier: MIT
// Copyright (c) 2026 imliubo

#include <unity.h>

#include <stdint.h>

#include "StickS3AgentStatus.h"
#include "StickS3ButtonController.h"
#include "StickS3PowerController.h"
#include "StickS3ScreenFlash.h"
#include "StopWatchButtonController.h"
#include "StopWatchNavigateControl.h"
#include "StopWatchSwipe.h"

namespace {

using codex_micro::StickS3ButtonAction;
using codex_micro::StickS3ButtonController;
using codex_micro::StickS3PowerAction;
using codex_micro::StickS3PowerController;
using codex_micro::StickS3ScreenFlash;
using codex_micro::StopWatchButtonAction;
using codex_micro::StopWatchButtonController;
using codex_micro::StopWatchDialGesture;

struct FakeAgentStatus {
  uint32_t color = 0;
  float brightness = 0.0f;
  uint8_t effect = 0;
  float speed = 0.0f;
};

struct FakeAgentStatuses {
  FakeAgentStatus values[6];
  size_t count = 6;

  size_t size() const { return count; }
  const FakeAgentStatus& operator[](size_t index) const { return values[index]; }
  FakeAgentStatus& operator[](size_t index) { return values[index]; }
};

void expectAction(StickS3ButtonAction actual, StickS3ButtonAction expected) {
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(expected.kind),
                          static_cast<uint8_t>(actual.kind));
}

void expectPowerAction(StickS3PowerAction actual,
                       StickS3PowerAction expected) {
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(expected),
                          static_cast<uint8_t>(actual));
}

void expectStopWatchAction(StopWatchButtonAction actual,
                           StopWatchButtonAction expected) {
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(expected),
                          static_cast<uint8_t>(actual));
}

void testStopWatchButtonSingleAndDoubleClicks() {
  StopWatchButtonController buttons;

  expectStopWatchAction(buttons.onButtonAReleased(100),
                        StopWatchButtonAction::None);
  expectStopWatchAction(buttons.pollButtonA(451, false),
                        StopWatchButtonAction::ButtonASingle);

  expectStopWatchAction(buttons.onButtonAReleased(1000),
                        StopWatchButtonAction::None);
  expectStopWatchAction(buttons.onButtonAReleased(1350),
                        StopWatchButtonAction::ButtonADouble);

  expectStopWatchAction(buttons.onButtonBReleased(2000),
                        StopWatchButtonAction::None);
  expectStopWatchAction(buttons.pollButtonB(2351, false),
                        StopWatchButtonAction::ButtonBSingle);

  expectStopWatchAction(buttons.onButtonBReleased(3000),
                        StopWatchButtonAction::None);
  expectStopWatchAction(buttons.onButtonBReleased(3350),
                        StopWatchButtonAction::ButtonBDouble);
}

void testStopWatchSingleClickWaitsWhileSecondPressIsHeld() {
  StopWatchButtonController buttons;

  buttons.onButtonAReleased(100);
  expectStopWatchAction(buttons.pollButtonA(1000, true),
                        StopWatchButtonAction::None);
  expectStopWatchAction(buttons.onButtonAReleased(1001),
                        StopWatchButtonAction::ButtonASingle);
  expectStopWatchAction(buttons.pollButtonA(1352, false),
                        StopWatchButtonAction::ButtonASingle);
}

void testStopWatchButtonTimersSurviveMillisRollover() {
  StopWatchButtonController buttons;
  constexpr uint32_t release = UINT32_MAX - 100;

  buttons.onButtonBReleased(release);
  expectStopWatchAction(buttons.pollButtonB(249, false),
                        StopWatchButtonAction::None);
  expectStopWatchAction(buttons.pollButtonB(250, false),
                        StopWatchButtonAction::ButtonBSingle);
}

            void testStopWatchHorizontalSwipesMapToPageDirection() {
              TEST_ASSERT_EQUAL_INT8(
                1, codex_micro::stopWatchSwipePageDelta(-90, 12));
              TEST_ASSERT_EQUAL_INT8(
                -1, codex_micro::stopWatchSwipePageDelta(90, -12));
            }

            void testStopWatchSwipeRejectsShortAndVerticalMotion() {
              TEST_ASSERT_EQUAL_INT8(
                0, codex_micro::stopWatchSwipePageDelta(69, 0));
              TEST_ASSERT_EQUAL_INT8(
                0, codex_micro::stopWatchSwipePageDelta(80, 90));
              TEST_ASSERT_EQUAL_INT8(
                0, codex_micro::stopWatchSwipePageDelta(-80, 80));
            }

void testStopWatchJoystickUsesContinuousAngleAndDistance() {
  const auto right =
      codex_micro::stopWatchJoystickPosition(10, 0, 0, 0, 10);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, right.angle);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, right.distance);

  const auto halfDown =
      codex_micro::stopWatchJoystickPosition(0, 5, 0, 0, 10);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.25f, halfDown.angle);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.5f, halfDown.distance);

  const auto up =
      codex_micro::stopWatchJoystickPosition(0, -20, 0, 0, 10);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.75f, up.angle);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, up.distance);
}

void testStopWatchDialTracksClockwiseAndCounterclockwiseSteps() {
  StopWatchDialGesture dial(4);
  dial.begin(10, 0, 0, 0);
  TEST_ASSERT_EQUAL_INT(1, dial.update(0, 10, 0, 0));
  TEST_ASSERT_EQUAL_INT(-1, dial.update(10, 0, 0, 0));
}

void testStopWatchDialHandlesAngleWraparound() {
  StopWatchDialGesture dial(16);
  dial.begin(-10, 2, 0, 0);
  TEST_ASSERT_EQUAL_INT(1, dial.update(-10, -2, 0, 0));
}

void testButtonASingleClickSendsEnterAfterWindow() {
  StickS3ButtonController buttons;

  expectAction(buttons.onButtonAReleased(100), StickS3ButtonAction::None);
  expectAction(buttons.pollButtonA(450), StickS3ButtonAction::None);
  expectAction(buttons.pollButtonA(451), StickS3ButtonAction::SendEnter);
  expectAction(buttons.pollButtonA(1000), StickS3ButtonAction::None);
}

void testButtonADoubleClickAtBoundarySendsRightAltOnly() {
  StickS3ButtonController buttons;

  expectAction(buttons.onButtonAReleased(100), StickS3ButtonAction::None);
  expectAction(buttons.onButtonAReleased(450),
               StickS3ButtonAction::SendRightAlt);
  expectAction(buttons.pollButtonA(1000), StickS3ButtonAction::None);
}

void testZeroDoubleClickWindowOnlyAcceptsSameTimestamp() {
  StickS3ButtonController sameTimestamp(6, 0);
  StickS3ButtonController nextTimestamp(6, 0);

  sameTimestamp.onButtonAReleased(100);
  expectAction(sameTimestamp.onButtonAReleased(100),
               StickS3ButtonAction::SendRightAlt);

  nextTimestamp.onButtonAReleased(100);
  expectAction(nextTimestamp.onButtonAReleased(101),
               StickS3ButtonAction::SendEnter);
  expectAction(nextTimestamp.pollButtonA(102), StickS3ButtonAction::SendEnter);
}

void testButtonBSingleClickSelectsNextAfterWindow() {
  StickS3ButtonController buttons;

  expectAction(buttons.onButtonBReleased(100, false),
               StickS3ButtonAction::None);
  expectAction(buttons.pollButtonB(450, false), StickS3ButtonAction::None);
  TEST_ASSERT_EQUAL_UINT8(0, buttons.selectedAgent());
  expectAction(buttons.pollButtonB(451, false),
               StickS3ButtonAction::SelectionChanged);
  TEST_ASSERT_EQUAL_UINT8(1, buttons.selectedAgent());
}

void testButtonBDoubleClickAtBoundarySelectsPreviousAndWraps() {
  StickS3ButtonController buttons;

  expectAction(buttons.onButtonBReleased(100, false),
               StickS3ButtonAction::None);
  expectAction(buttons.onButtonBReleased(450, false),
               StickS3ButtonAction::SelectionChanged);
  TEST_ASSERT_EQUAL_UINT8(5, buttons.selectedAgent());
  expectAction(buttons.pollButtonB(1000, false), StickS3ButtonAction::None);
}

void testButtonBHoldActivatesAgentAndCancelsPendingClick() {
  StickS3ButtonController buttons;

  buttons.onButtonBReleased(100, false);
  expectAction(buttons.onButtonBHold(),
               StickS3ButtonAction::ActivateSelectedAgent);
  TEST_ASSERT_EQUAL_UINT8(0, buttons.selectedAgent());
  expectAction(buttons.onButtonBReleased(700, true),
               StickS3ButtonAction::None);
  expectAction(buttons.pollButtonB(1200, false), StickS3ButtonAction::None);
}

void testButtonBPendingClickWaitsWhileSecondPressIsHeld() {
  StickS3ButtonController buttons;

  buttons.onButtonBReleased(100, false);
  expectAction(buttons.pollButtonB(1000, true), StickS3ButtonAction::None);
  TEST_ASSERT_EQUAL_UINT8(0, buttons.selectedAgent());
  expectAction(buttons.onButtonBHold(),
               StickS3ButtonAction::ActivateSelectedAgent);
  TEST_ASSERT_EQUAL_UINT8(0, buttons.selectedAgent());
}

void testLateSecondClicksPreserveBothSingleClicks() {
  StickS3ButtonController buttons;

  buttons.onButtonAReleased(100);
  expectAction(buttons.onButtonAReleased(451),
               StickS3ButtonAction::SendEnter);
  expectAction(buttons.pollButtonA(802), StickS3ButtonAction::SendEnter);

  buttons.onButtonBReleased(1000, false);
  expectAction(buttons.onButtonBReleased(1351, false),
               StickS3ButtonAction::SelectionChanged);
  TEST_ASSERT_EQUAL_UINT8(1, buttons.selectedAgent());
  expectAction(buttons.pollButtonB(1702, false),
               StickS3ButtonAction::SelectionChanged);
  TEST_ASSERT_EQUAL_UINT8(2, buttons.selectedAgent());
}

void testAgentSelectionWrapsAfterSixSingleClicks() {
  StickS3ButtonController buttons;
  uint32_t now = 0;

  for (uint8_t i = 0; i < 6; ++i) {
    expectAction(buttons.onButtonBReleased(now, false),
                 StickS3ButtonAction::None);
    now += 351;
    expectAction(buttons.pollButtonB(now, false),
                 StickS3ButtonAction::SelectionChanged);
    ++now;
  }
  TEST_ASSERT_EQUAL_UINT8(0, buttons.selectedAgent());
}

void testTimersSurviveMillisRollover() {
  StickS3ButtonController buttons;
  constexpr uint32_t release = UINT32_MAX - 100;

  buttons.onButtonBReleased(release, false);
  expectAction(buttons.pollButtonB(249, false), StickS3ButtonAction::None);
  expectAction(buttons.pollButtonB(250, false),
               StickS3ButtonAction::SelectionChanged);
  TEST_ASSERT_EQUAL_UINT8(1, buttons.selectedAgent());
}

void testButtonATimersSurviveMillisRollover() {
  constexpr uint32_t release = UINT32_MAX - 100;
  StickS3ButtonController singleClick;
  StickS3ButtonController doubleClick;

  singleClick.onButtonAReleased(release);
  expectAction(singleClick.pollButtonA(249), StickS3ButtonAction::None);
  expectAction(singleClick.pollButtonA(250), StickS3ButtonAction::SendEnter);

  doubleClick.onButtonAReleased(release);
  expectAction(doubleClick.onButtonAReleased(249),
               StickS3ButtonAction::SendRightAlt);
  expectAction(doubleClick.pollButtonA(1000), StickS3ButtonAction::None);
}

void testButtonGesturesCanBeRemapped() {
  StickS3ButtonController buttons;
  buttons.setAction(codex_micro::StickS3ButtonSlot::ButtonASingle,
                    codex_micro::makeStickS3SelectionAction(1));
  buttons.setAction(codex_micro::StickS3ButtonSlot::ButtonBHold,
                    codex_micro::makeStickS3KeyboardAction(0x03, 0x07));

  buttons.onButtonAReleased(100);
  expectAction(buttons.pollButtonA(451),
               StickS3ButtonAction::SelectionChanged);
  TEST_ASSERT_EQUAL_UINT8(1, buttons.selectedAgent());

  const StickS3ButtonAction shortcut = buttons.onButtonBHold();
  expectAction(shortcut, StickS3ButtonAction::KeyboardShortcut);
  TEST_ASSERT_EQUAL_HEX8(0x03, shortcut.modifier);
  TEST_ASSERT_EQUAL_HEX8(0x07, shortcut.key);
}

void testButtonActionTokensAreValidated() {
  StickS3ButtonAction action;

  TEST_ASSERT_TRUE(codex_micro::parseStickS3ButtonAction("enter", action));
  expectAction(action, StickS3ButtonAction::SendEnter);

  TEST_ASSERT_TRUE(
      codex_micro::parseStickS3ButtonAction("key:3:7", action));
  expectAction(action, StickS3ButtonAction::KeyboardShortcut);
  TEST_ASSERT_EQUAL_HEX8(0x03, action.modifier);
  TEST_ASSERT_EQUAL_HEX8(0x07, action.key);

  TEST_ASSERT_TRUE(
      codex_micro::parseStickS3ButtonAction("codex:ACT12", action));
  expectAction(action, StickS3ButtonAction::CodexCommand);
  TEST_ASSERT_EQUAL_STRING("ACT12",
                           codex_micro::stickS3CodexCommandKey(
                               action.commandIndex));

  TEST_ASSERT_FALSE(
      codex_micro::parseStickS3ButtonAction("key:256:7", action));
  TEST_ASSERT_FALSE(
      codex_micro::parseStickS3ButtonAction("key:0:0", action));
  TEST_ASSERT_FALSE(
      codex_micro::parseStickS3ButtonAction("codex:ACT99", action));
}

void testButtonBDoubleClickMovesBackFromNonzeroAgent() {
  StickS3ButtonController buttons(3);

  buttons.onButtonBReleased(100, false);
  expectAction(buttons.pollButtonB(451, false),
               StickS3ButtonAction::SelectionChanged);
  TEST_ASSERT_EQUAL_UINT8(1, buttons.selectedAgent());

  buttons.onButtonBReleased(500, false);
  expectAction(buttons.onButtonBReleased(600, false),
               StickS3ButtonAction::SelectionChanged);
  TEST_ASSERT_EQUAL_UINT8(0, buttons.selectedAgent());
}

void testReleaseAfterHoldDoesNotStartPendingButtonBClick() {
  StickS3ButtonController buttons;

  expectAction(buttons.onButtonBReleased(100, true),
               StickS3ButtonAction::None);
  expectAction(buttons.pollButtonB(1000, false), StickS3ButtonAction::None);
  TEST_ASSERT_EQUAL_UINT8(0, buttons.selectedAgent());

  buttons.onButtonBReleased(1100, false);
  expectAction(buttons.pollButtonB(1451, false),
               StickS3ButtonAction::SelectionChanged);
  TEST_ASSERT_EQUAL_UINT8(1, buttons.selectedAgent());
}

void testZeroAgentCountFallsBackToOneAgent() {
  StickS3ButtonController buttons(0);

  buttons.onButtonBReleased(100, false);
  expectAction(buttons.pollButtonB(451, false),
               StickS3ButtonAction::SelectionChanged);
  TEST_ASSERT_EQUAL_UINT8(0, buttons.selectedAgent());
  buttons.onButtonBReleased(1000, false);
  expectAction(buttons.onButtonBReleased(1100, false),
               StickS3ButtonAction::SelectionChanged);
  TEST_ASSERT_EQUAL_UINT8(0, buttons.selectedAgent());
}

void testScreenFlashRunsExactlyThreeTimes() {
  StickS3ScreenFlash flash(100, 3);

  flash.start(1000);
  TEST_ASSERT_TRUE(flash.active());
  TEST_ASSERT_TRUE(flash.visible());

  for (uint8_t phase = 1; phase < 6; ++phase) {
    TEST_ASSERT_FALSE(flash.update(1000 + phase * 100 - 1));
    TEST_ASSERT_TRUE(flash.update(1000 + phase * 100));
    TEST_ASSERT_EQUAL((phase % 2) == 0, flash.visible());
  }

  TEST_ASSERT_FALSE(flash.visible());
  TEST_ASSERT_TRUE(flash.active());
  TEST_ASSERT_FALSE(flash.update(1599));
  TEST_ASSERT_FALSE(flash.update(1600));
  TEST_ASSERT_FALSE(flash.active());
  TEST_ASSERT_FALSE(flash.visible());
}

void testScreenFlashSurvivesMillisRollover() {
  StickS3ScreenFlash flash(100, 1);
  constexpr uint32_t start = UINT32_MAX - 49;

  flash.start(start);
  TEST_ASSERT_FALSE(flash.update(49));
  TEST_ASSERT_TRUE(flash.update(50));
  TEST_ASSERT_FALSE(flash.visible());
}

void testRestartingScreenFlashStartsAFullSequence() {
  StickS3ScreenFlash flash(100, 3);

  flash.start(0);
  TEST_ASSERT_TRUE(flash.update(100));
  TEST_ASSERT_FALSE(flash.visible());
  flash.start(150);
  TEST_ASSERT_TRUE(flash.visible());
  TEST_ASSERT_FALSE(flash.update(249));
  TEST_ASSERT_TRUE(flash.update(250));
  TEST_ASSERT_FALSE(flash.visible());
  TEST_ASSERT_TRUE(flash.active());
}

void testZeroFlashCountNeverStartsAnimation() {
  StickS3ScreenFlash flash(100, 0);

  flash.start(1000);
  TEST_ASSERT_FALSE(flash.active());
  TEST_ASSERT_FALSE(flash.visible());
  TEST_ASSERT_FALSE(flash.update(2000));
}

void testZeroFlashPhaseDurationFallsBackToOneMillisecond() {
  StickS3ScreenFlash flash(0, 1);

  flash.start(100);
  TEST_ASSERT_FALSE(flash.update(100));
  TEST_ASSERT_TRUE(flash.visible());
  TEST_ASSERT_TRUE(flash.update(101));
  TEST_ASSERT_FALSE(flash.visible());
  TEST_ASSERT_TRUE(flash.active());
  TEST_ASSERT_FALSE(flash.update(102));
  TEST_ASSERT_FALSE(flash.active());
}

void testDelayedFlashPollingStillShowsEveryPhase() {
  StickS3ScreenFlash flash(100, 2);

  flash.start(0);
  TEST_ASSERT_TRUE(flash.update(1000));
  TEST_ASSERT_FALSE(flash.visible());
  TEST_ASSERT_TRUE(flash.active());
  TEST_ASSERT_FALSE(flash.update(1000));
  TEST_ASSERT_TRUE(flash.update(1100));
  TEST_ASSERT_TRUE(flash.visible());
  TEST_ASSERT_TRUE(flash.update(1200));
  TEST_ASSERT_FALSE(flash.visible());
  TEST_ASSERT_FALSE(flash.update(1300));
  TEST_ASSERT_FALSE(flash.active());
}

void testLargeFlashCountDoesNotOverflowPhaseCounter() {
  StickS3ScreenFlash flash(1, 128);

  flash.start(0);
  TEST_ASSERT_TRUE(flash.active());
  for (uint16_t phase = 1; phase < 256; ++phase) {
    TEST_ASSERT_TRUE(flash.update(phase));
    TEST_ASSERT_TRUE(flash.active());
  }
  TEST_ASSERT_FALSE(flash.update(256));
  TEST_ASSERT_FALSE(flash.active());
  TEST_ASSERT_FALSE(flash.visible());
}

void testOnlyActualAgentStatusChangesTriggerFlash() {
  FakeAgentStatuses before;
  FakeAgentStatuses after = before;

  TEST_ASSERT_FALSE(codex_micro::agentStatusesChanged(before, after));

  after[4].color = 0x123456;
  TEST_ASSERT_TRUE(codex_micro::agentStatusesChanged(before, after));
  after = before;
  after[4].brightness = 0.5f;
  TEST_ASSERT_TRUE(codex_micro::agentStatusesChanged(before, after));
  after = before;
  after[4].effect = 1;
  TEST_ASSERT_TRUE(codex_micro::agentStatusesChanged(before, after));
  after = before;
  after[4].speed = 2.0f;
  TEST_ASSERT_TRUE(codex_micro::agentStatusesChanged(before, after));
}

void testStatusChangeInEveryAgentSlotTriggersFlash() {
  FakeAgentStatuses before;

  for (size_t agent = 0; agent < before.size(); ++agent) {
    FakeAgentStatuses after = before;
    after[agent].color = static_cast<uint32_t>(agent + 1);
    TEST_ASSERT_TRUE(codex_micro::agentStatusesChanged(before, after));
  }
}

void testIdenticalNondefaultAgentStatusesDoNotTriggerFlash() {
  FakeAgentStatuses before;
  before[0].color = 0xABCDEF;
  before[0].brightness = 0.75f;
  before[0].effect = 2;
  before[0].speed = 1.5f;
  FakeAgentStatuses after = before;

  TEST_ASSERT_FALSE(codex_micro::agentStatusesChanged(before, after));
}

void testAgentStatusCollectionSizeChangeTriggersFlash() {
  FakeAgentStatuses before;
  FakeAgentStatuses after = before;
  after.count = 5;

  TEST_ASSERT_TRUE(codex_micro::agentStatusesChanged(before, after));
}

void testEmptyAgentStatusCollectionsAreUnchanged() {
  FakeAgentStatuses before;
  FakeAgentStatuses after;
  before.count = 0;
  after.count = 0;

  TEST_ASSERT_FALSE(codex_micro::agentStatusesChanged(before, after));
}

void testPowerControllerDimsAtOneMinuteBoundary() {
  StickS3PowerController power;
  power.begin(1000);

  expectPowerAction(power.update(60999), StickS3PowerAction::None);
  expectPowerAction(power.update(61000), StickS3PowerAction::DimDisplay);
  expectPowerAction(power.update(62000), StickS3PowerAction::None);
}

void testActivityRestoresBrightnessAndRestartsTimeouts() {
  StickS3PowerController power;
  power.begin(0);
  expectPowerAction(power.update(60000), StickS3PowerAction::DimDisplay);

  expectPowerAction(power.onActivity(70000),
                    StickS3PowerAction::RestoreDisplay);
  expectPowerAction(power.update(129999), StickS3PowerAction::None);
  expectPowerAction(power.update(130000), StickS3PowerAction::DimDisplay);
  expectPowerAction(power.onActivity(140000),
                    StickS3PowerAction::RestoreDisplay);
}

void testActivityWhileBrightOnlyRestartsTimeouts() {
  StickS3PowerController power;
  power.begin(1000);

  expectPowerAction(power.onActivity(50000), StickS3PowerAction::None);
  expectPowerAction(power.update(109999), StickS3PowerAction::None);
  expectPowerAction(power.update(110000), StickS3PowerAction::DimDisplay);
}

void testPowerControllerPowersOffAtThirtyMinuteBoundaryOnce() {
  StickS3PowerController power;
  power.begin(1000);

  expectPowerAction(power.update(61000), StickS3PowerAction::DimDisplay);
  expectPowerAction(power.update(1800999), StickS3PowerAction::None);
  expectPowerAction(power.update(1801000), StickS3PowerAction::PowerOff);
  expectPowerAction(power.update(1802000), StickS3PowerAction::None);
}

void testLatePowerPollSkipsDimAndPowersOffImmediately() {
  StickS3PowerController power;
  power.begin(0);

  expectPowerAction(power.update(1800000), StickS3PowerAction::PowerOff);
}

void testPowerTimeoutsSurviveMillisRollover() {
  StickS3PowerController power;
  constexpr uint32_t start = UINT32_MAX - 30000;
  power.begin(start);

  expectPowerAction(power.update(29998), StickS3PowerAction::None);
  expectPowerAction(power.update(29999), StickS3PowerAction::DimDisplay);
  expectPowerAction(power.onActivity(40000),
                    StickS3PowerAction::RestoreDisplay);
  expectPowerAction(power.update(1840000), StickS3PowerAction::PowerOff);
}

void testFirstPowerUpdateInitializesActivityTimer() {
  StickS3PowerController power;

  expectPowerAction(power.update(5000), StickS3PowerAction::None);
  expectPowerAction(power.update(64999), StickS3PowerAction::None);
  expectPowerAction(power.update(65000), StickS3PowerAction::DimDisplay);
}

void testPowerTimeoutsCanBeDisabled() {
  StickS3PowerController power(0, 0);
  power.begin(0);

  expectPowerAction(power.update(UINT32_MAX), StickS3PowerAction::None);
}

void testChangingPowerTimeoutsRestoresDisplayAndRestartsTimer() {
  StickS3PowerController power;
  power.begin(0);
  expectPowerAction(power.update(60000), StickS3PowerAction::DimDisplay);

  expectPowerAction(power.setTimeouts(120000, 600000, 70000),
                    StickS3PowerAction::RestoreDisplay);
  expectPowerAction(power.update(189999), StickS3PowerAction::None);
  expectPowerAction(power.update(190000), StickS3PowerAction::DimDisplay);
  expectPowerAction(power.update(670000), StickS3PowerAction::PowerOff);
}

}  // namespace

void setUp() {}
void tearDown() {}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(testStopWatchButtonSingleAndDoubleClicks);
  RUN_TEST(testStopWatchSingleClickWaitsWhileSecondPressIsHeld);
  RUN_TEST(testStopWatchButtonTimersSurviveMillisRollover);
  RUN_TEST(testStopWatchHorizontalSwipesMapToPageDirection);
  RUN_TEST(testStopWatchSwipeRejectsShortAndVerticalMotion);
  RUN_TEST(testStopWatchJoystickUsesContinuousAngleAndDistance);
  RUN_TEST(testStopWatchDialTracksClockwiseAndCounterclockwiseSteps);
  RUN_TEST(testStopWatchDialHandlesAngleWraparound);
  RUN_TEST(testButtonASingleClickSendsEnterAfterWindow);
  RUN_TEST(testButtonADoubleClickAtBoundarySendsRightAltOnly);
  RUN_TEST(testZeroDoubleClickWindowOnlyAcceptsSameTimestamp);
  RUN_TEST(testButtonBSingleClickSelectsNextAfterWindow);
  RUN_TEST(testButtonBDoubleClickAtBoundarySelectsPreviousAndWraps);
  RUN_TEST(testButtonBHoldActivatesAgentAndCancelsPendingClick);
  RUN_TEST(testButtonBPendingClickWaitsWhileSecondPressIsHeld);
  RUN_TEST(testLateSecondClicksPreserveBothSingleClicks);
  RUN_TEST(testAgentSelectionWrapsAfterSixSingleClicks);
  RUN_TEST(testTimersSurviveMillisRollover);
  RUN_TEST(testButtonATimersSurviveMillisRollover);
  RUN_TEST(testButtonGesturesCanBeRemapped);
  RUN_TEST(testButtonActionTokensAreValidated);
  RUN_TEST(testButtonBDoubleClickMovesBackFromNonzeroAgent);
  RUN_TEST(testReleaseAfterHoldDoesNotStartPendingButtonBClick);
  RUN_TEST(testZeroAgentCountFallsBackToOneAgent);
  RUN_TEST(testScreenFlashRunsExactlyThreeTimes);
  RUN_TEST(testScreenFlashSurvivesMillisRollover);
  RUN_TEST(testRestartingScreenFlashStartsAFullSequence);
  RUN_TEST(testZeroFlashCountNeverStartsAnimation);
  RUN_TEST(testZeroFlashPhaseDurationFallsBackToOneMillisecond);
  RUN_TEST(testDelayedFlashPollingStillShowsEveryPhase);
  RUN_TEST(testLargeFlashCountDoesNotOverflowPhaseCounter);
  RUN_TEST(testOnlyActualAgentStatusChangesTriggerFlash);
  RUN_TEST(testStatusChangeInEveryAgentSlotTriggersFlash);
  RUN_TEST(testIdenticalNondefaultAgentStatusesDoNotTriggerFlash);
  RUN_TEST(testAgentStatusCollectionSizeChangeTriggersFlash);
  RUN_TEST(testEmptyAgentStatusCollectionsAreUnchanged);
  RUN_TEST(testPowerControllerDimsAtOneMinuteBoundary);
  RUN_TEST(testActivityRestoresBrightnessAndRestartsTimeouts);
  RUN_TEST(testActivityWhileBrightOnlyRestartsTimeouts);
  RUN_TEST(testPowerControllerPowersOffAtThirtyMinuteBoundaryOnce);
  RUN_TEST(testLatePowerPollSkipsDimAndPowersOffImmediately);
  RUN_TEST(testPowerTimeoutsSurviveMillisRollover);
  RUN_TEST(testFirstPowerUpdateInitializesActivityTimer);
  RUN_TEST(testPowerTimeoutsCanBeDisabled);
  RUN_TEST(testChangingPowerTimeoutsRestoresDisplayAndRestartsTimer);
  return UNITY_END();
}
