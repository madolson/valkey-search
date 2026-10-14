/*
 * Copyright (c) 2025, valkey-search contributors
 * All rights reserved.
 * SPDX-License-Identifier: BSD 3-Clause
 *
 */

#ifndef VMSDK_SRC_MEMORY_ALLOCATION_H_
#define VMSDK_SRC_MEMORY_ALLOCATION_H_

#include <cstddef>
#include <cstdint>

namespace vmsdk {

// Report used memory counter.
uint64_t GetUsedMemoryCnt();

void ReportAllocMemorySize(uint64_t size);
void ReportFreeMemorySize(uint64_t size);

int64_t GetMemoryDelta();

void SetMemoryDelta(int64_t delta);

// Valkey exposes no aligned allocation entry point. jemalloc returns memory
// aligned to the size class, so rounding the request up to a multiple of the
// alignment gets us the alignment we need.
//
// See https://linux.die.net/man/3/jemalloc: "... Chunks are always aligned to
// multiples of the chunk size..."
inline size_t AlignSize(size_t size, size_t alignment = 16) {
  return (size + alignment - 1) & ~(alignment - 1);
}

// The module's calloc. It lives here rather than in memory_allocation_c_api.cc
// so that the unit tests, which do not link that file, can reach it.
void* Calloc(size_t nmemb, size_t size);

}  // namespace vmsdk

#endif  // VMSDK_SRC_MEMORY_ALLOCATION_H_
