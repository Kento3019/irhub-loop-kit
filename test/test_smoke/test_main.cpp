// native テスト環境そのものが動くかの確認。
#include <unity.h>
#include "core_version.h"

void setUp() {}
void tearDown() {}

void test_core_is_linked() {
  TEST_ASSERT_EQUAL_STRING("0.1.0", irhub::coreVersion());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_core_is_linked);
  return UNITY_END();
}
