/*
 * Copyright (c) 2025, valkey-search contributors
 * All rights reserved.
 * SPDX-License-Identifier: BSD 3-Clause
 *
 */

#include "vmsdk/src/memory_allocation.h"

#include <unistd.h>

#include <cerrno>
#include <cstdint>

#include "absl/base/optimization.h"
#include "vmsdk/src/sharded_atomic.h"
#include "vmsdk/src/valkey_module_api/valkey_module.h"

namespace vmsdk {

// Use the standard system allocator by default. Note that this is required
// since any allocation done before Valkey module initialization (namely global
// static constructors that do heap allocation, which are run on dl_open) cannot
// invoke Valkey modules api since the associated C function pointers are only
// initialized as part of the module initialization process. Refer
// https://redis.com/blog/using-the-redis-allocator-in-rust for more details.

thread_local static int64_t memory_delta = 0;

ShardedAtomic<uint64_t> used_memory_bytes;

uint64_t GetUsedMemoryCnt() { return used_memory_bytes.GetTotal(); }

void ReportAllocMemorySize(uint64_t size) {
  used_memory_bytes.Add(size);

  memory_delta += static_cast<int64_t>(size);
}

void ReportFreeMemorySize(uint64_t size) {
  used_memory_bytes.Subtract(size);
  memory_delta -= static_cast<int64_t>(size);
}

int64_t GetMemoryDelta() { return memory_delta; }

void SetMemoryDelta(int64_t delta) { memory_delta = delta; }

void* Calloc(size_t nmemb, size_t size) {
  // Alignment depends only on the size of the whole block, so round the total.
  // Rounding each element instead inflates small elements up to 16x. Valkey's
  // Calloc multiplies its arguments without checking, so the product, and the
  // rounding of it, are checked here.
  size_t total;
  if (ABSL_PREDICT_FALSE(__builtin_mul_overflow(nmemb, size, &total) ||
                         AlignSize(total) < total)) {
    errno = ENOMEM;
    return nullptr;
  }
  void* ptr = ValkeyModule_Calloc(1, AlignSize(total));
  if (ABSL_PREDICT_TRUE(ptr != nullptr)) {
    ReportAllocMemorySize(ValkeyModule_MallocUsableSize(ptr));
  }
  return ptr;
}

}  // namespace vmsdk
