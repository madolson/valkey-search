/*
 * Copyright (c) 2025, valkey-search contributors
 * All rights reserved.
 * SPDX-License-Identifier: BSD 3-Clause
 *
 */

#include "vmsdk/src/memory_allocation.h"

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdlib>

#include "gtest/gtest.h"
#include "vmsdk/src/testing_infra/module.h"
#include "vmsdk/src/testing_infra/utils.h"
#include "vmsdk/src/valkey_module_api/valkey_module.h"

namespace vmsdk {

namespace {

// What Calloc last asked Valkey for.
int calloc_calls;
size_t calloc_nmemb;
size_t calloc_size;

void* RecordingCalloc(size_t nmemb, size_t size) {
  calloc_calls++;
  calloc_nmemb = nmemb;
  calloc_size = size;
  return std::calloc(nmemb, size);
}

class CallocTest : public vmsdk::ValkeyTest {
 protected:
  void SetUp() override {
    ValkeyTest::SetUp();
    calloc_calls = 0;
    ValkeyModule_Calloc = &RecordingCalloc;
  }
  void TearDown() override {
    ValkeyModule_Calloc = &TestValkeyModule_Calloc;
    ValkeyTest::TearDown();
  }
};

TEST_F(CallocTest, RoundsTheTotalNotEachElement) {
  struct {
    size_t nmemb;
    size_t size;
    size_t expected;
  } cases[] = {
      {64, 1, 64}, {3, 8, 32}, {1, 1, 16}, {5, 16, 80}, {0, 1, 0},
  };
  for (const auto& c : cases) {
    calloc_calls = 0;
    void* ptr = Calloc(c.nmemb, c.size);
    ASSERT_NE(ptr, nullptr);
    EXPECT_EQ(calloc_calls, 1);
    EXPECT_EQ(calloc_nmemb * calloc_size, c.expected)
        << "calloc(" << c.nmemb << ", " << c.size << ")";
    std::free(ptr);
  }
}

TEST_F(CallocTest, RejectsOverflowingProduct) {
  errno = 0;
  EXPECT_EQ(Calloc(2, SIZE_MAX / 2 + 1), nullptr);
  EXPECT_EQ(errno, ENOMEM);
  EXPECT_EQ(calloc_calls, 0);
}

TEST_F(CallocTest, RejectsOverflowingRounding) {
  errno = 0;
  EXPECT_EQ(Calloc(1, SIZE_MAX), nullptr);
  EXPECT_EQ(errno, ENOMEM);
  EXPECT_EQ(calloc_calls, 0);
}

}  // namespace

}  // namespace vmsdk
