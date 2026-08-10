// SPDX-License-Identifier: MIT
// Copyright (c) 2026 imliubo

#include <unity.h>

#include "CoreAgentCardStyle.h"

namespace {

constexpr uint16_t kPanel = 0x18E3;

void testAssignedAgentUsesStatusColorAsCardFill() {
  TEST_ASSERT_EQUAL_HEX16(
      0x2E73, codex_micro::agentCardFill(0x2E73, true, false, kPanel));
}

void testUnassignedAgentKeepsNeutralCardFill() {
  TEST_ASSERT_EQUAL_HEX16(
      kPanel, codex_micro::agentCardFill(0xF800, false, false, kPanel));
}

void testPressedAgentKeepsStatusHueAndDimsFill() {
  TEST_ASSERT_EQUAL_HEX16(
      codex_micro::dimRgb565(0x2E73),
      codex_micro::agentCardFill(0x2E73, true, true, kPanel));
}

void testTextContrastsWithCardFill() {
  TEST_ASSERT_EQUAL_HEX16(
      0x0841, codex_micro::agentCardTextColor(0xFFE0, 0xFFFF, 0x0841));
  TEST_ASSERT_EQUAL_HEX16(
      0xFFFF, codex_micro::agentCardTextColor(0x001F, 0xFFFF, 0x0841));
}

}  // namespace

void setUp() {}
void tearDown() {}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(testAssignedAgentUsesStatusColorAsCardFill);
  RUN_TEST(testUnassignedAgentKeepsNeutralCardFill);
  RUN_TEST(testPressedAgentKeepsStatusHueAndDimsFill);
  RUN_TEST(testTextContrastsWithCardFill);
  return UNITY_END();
}
