/*
 * Copyright (C) 2014-2021 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE. 
 */

#pragma once

#include "BPlatform.h"

BALLOW_UNSAFE_BUFFER_USAGE_BEGIN

#include "AllocationCounts.h"
#include "AvailableMemory.h"
#include "Cache.h"
#include "CompactAllocationMode.h"
#include "Gigacage.h"
#include "Heap.h"
#include "IsoTLS.h"
#include "Mutex.h"
#include "PerHeapKind.h"
#include "Scavenger.h"
#include "SystemHeap.h"

#if BUSE(LIBPAS)
#include "bmalloc_heap_inlines.h"
#endif

namespace bmalloc {
namespace api {

#if BUSE(LIBPAS)
extern pas_primitive_heap_ref gigacageHeaps[static_cast<size_t>(Gigacage::NumberOfKinds)];

inline pas_primitive_heap_ref& heapForKind(Gigacage::Kind kind)
{
    RELEASE_BASSERT(static_cast<size_t>(kind) < static_cast<size_t>(Gigacage::NumberOfKinds));
    return gigacageHeaps[static_cast<size_t>(kind)];
}
#endif

// Returns null on failure.
BINLINE void* tryMalloc(size_t size, CompactAllocationMode mode, HeapKind kind = HeapKind::Primary)
{
#if BUSE(LIBPAS)
    if (!isGigacage(kind))
        return bmalloc_try_allocate_inline(size, asPasAllocationMode(mode));
    return bmalloc_try_allocate_auxiliary_inline(&heapForKind(gigacageKind(kind)), size, asPasAllocationMode(mode));
#else
    BUNUSED(mode);
    return Cache::tryAllocate(kind, size);
#endif
}

// Crashes on failure.
BINLINE void* malloc(size_t size, CompactAllocationMode mode, HeapKind kind = HeapKind::Primary)
{
#if BUSE(LIBPAS)
    if (!isGigacage(kind))
        return bmalloc_allocate_inline(size, asPasAllocationMode(mode));
    return bmalloc_allocate_auxiliary_inline(&heapForKind(gigacageKind(kind)), size, asPasAllocationMode(mode));
#else
    BUNUSED(mode);
    return Cache::allocate(kind, size);
#endif
}

BINLINE void* tryZeroedMalloc(size_t size, CompactAllocationMode mode, HeapKind kind = HeapKind::Primary)
{
#if BUSE(LIBPAS)
    if (!isGigacage(kind))
        return bmalloc_try_allocate_zeroed_inline(size, asPasAllocationMode(mode));
    return bmalloc_try_allocate_auxiliary_zeroed_inline(&heapForKind(gigacageKind(kind)), size, asPasAllocationMode(mode));
#else
    BUNUSED(mode);
    auto* mem = Cache::tryAllocate(kind, size);
    if (mem)
        memset(mem, 0, size);
    return mem;
#endif
}

// Crashes on failure.
BINLINE void* zeroedMalloc(size_t size, CompactAllocationMode mode, HeapKind kind = HeapKind::Primary)
{
#if BUSE(LIBPAS)
    if (!isGigacage(kind))
        return bmalloc_allocate_zeroed_inline(size, asPasAllocationMode(mode));
    return bmalloc_allocate_auxiliary_zeroed_inline(&heapForKind(gigacageKind(kind)), size, asPasAllocationMode(mode));
#else
    BUNUSED(mode);
    auto* mem = Cache::allocate(kind, size);
    memset(mem, 0, size);
    return mem;
#endif
}

BEXPORT void* mallocOutOfLine(size_t size, CompactAllocationMode mode, HeapKind kind = HeapKind::Primary);

// Returns null on failure.
BINLINE void* tryMemalign(size_t alignment, size_t size, CompactAllocationMode mode, HeapKind kind = HeapKind::Primary)
{
#if BUSE(LIBPAS)
    if (!isGigacage(kind))
        return bmalloc_try_allocate_with_alignment_inline(size, alignment, asPasAllocationMode(mode));
    return bmalloc_try_allocate_auxiliary_with_alignment_inline(
        &heapForKind(gigacageKind(kind)), size, alignment, asPasAllocationMode(mode));
#else
    BUNUSED(mode);
    return Cache::tryAllocate(kind, alignment, size);
#endif
}

// Crashes on failure.
BINLINE void* memalign(size_t alignment, size_t size, CompactAllocationMode mode, HeapKind kind = HeapKind::Primary)
{
#if BUSE(LIBPAS)
    if (!isGigacage(kind))
        return bmalloc_allocate_with_alignment_inline(size, alignment, asPasAllocationMode(mode));
    return bmalloc_allocate_auxiliary_with_alignment_inline(
        &heapForKind(gigacageKind(kind)), size, alignment, asPasAllocationMode(mode));
#else
    BUNUSED(mode);
    return Cache::allocate(kind, alignment, size);
#endif
}

// Returns null on failure.
BINLINE void* tryRealloc(void* object, size_t newSize, CompactAllocationMode mode, HeapKind kind = HeapKind::Primary)
{
#if BUSE(LIBPAS)
    if (!isGigacage(kind)) {
        return bmalloc_try_reallocate_inline(
            object, newSize, asPasAllocationMode(mode), pas_reallocate_free_if_successful);
    }
    return bmalloc_try_reallocate_auxiliary_inline(
        object, &heapForKind(gigacageKind(kind)), newSize, asPasAllocationMode(mode), pas_reallocate_free_if_successful);
#else
    BUNUSED(mode);
    return Cache::tryReallocate(kind, object, newSize);
#endif
}

// Crashes on failure.
BINLINE void* realloc(void* object, size_t newSize, CompactAllocationMode mode, HeapKind kind = HeapKind::Primary)
{
#if BUSE(LIBPAS)
    if (!isGigacage(kind))
        return bmalloc_reallocate_inline(object, newSize, asPasAllocationMode(mode), pas_reallocate_free_if_successful);
    return bmalloc_reallocate_auxiliary_inline(
        object, &heapForKind(gigacageKind(kind)), newSize, asPasAllocationMode(mode), pas_reallocate_free_if_successful);
#else
    BUNUSED(mode);
    return Cache::reallocate(kind, object, newSize);
#endif
}

// Returns null on failure.
// This API will give you zeroed pages that are ready to be used. These pages
// will page fault on first access. It returns to you memory that initially only
// uses up virtual address space, not `size` bytes of physical memory.
BEXPORT void* tryLargeZeroedMemalignVirtual(size_t alignment, size_t size, CompactAllocationMode mode, HeapKind kind = HeapKind::Primary);

BINLINE void free(void* object, HeapKind kind = HeapKind::Primary)
{
#if BUSE(LIBPAS)
    BUNUSED(kind);
    bmalloc_deallocate_inline(object);
#else
    Cache::deallocate(kind, object);
#endif
}

BEXPORT void freeOutOfLine(void* object, HeapKind kind = HeapKind::Primary);

BEXPORT void freeLargeVirtual(void* object, size_t, HeapKind kind = HeapKind::Primary);

BEXPORT void scavengeThisThread();

BEXPORT void scavenge();

BEXPORT bool isEnabled(HeapKind kind = HeapKind::Primary);

// ptr must be aligned to vmPageSizePhysical and size must be divisible 
// by vmPageSizePhysical.
BEXPORT void decommitAlignedPhysical(void* object, size_t, HeapKind = HeapKind::Primary);
BEXPORT void commitAlignedPhysical(void* object, size_t, HeapKind = HeapKind::Primary);
    
inline size_t availableMemory()
{
    return bmalloc::availableMemory();
}
    
#if BPLATFORM(IOS_FAMILY) || BOS(LINUX) || BOS(FREEBSD)
inline size_t memoryFootprint()
{
    return bmalloc::memoryFootprint();
}

inline double percentAvailableMemoryInUse()
{
    return bmalloc::percentAvailableMemoryInUse();
}
#endif

#if BOS(DARWIN)
BEXPORT void setScavengerThreadQOSClass(qos_class_t overrideClass);
#endif

BEXPORT void enableMiniMode(bool forceMiniMode = false);

// Used for debugging only.
BEXPORT void disableScavenger();
BEXPORT void forceEnablePGM(uint16_t guardMallocRate);

#if BENABLE(MALLOC_SIZE)
inline size_t mallocSize(const void* object)
{
    if (auto* systemHeap = SystemHeap::tryGet())
        return systemHeap->mallocSize(object);
    return bmalloc_get_allocation_size(const_cast<void*>(object));
}
#endif

#if BENABLE(MALLOC_GOOD_SIZE)
inline size_t mallocGoodSize(size_t size)
{
    if (auto* systemHeap = SystemHeap::tryGet())
        return systemHeap->mallocGoodSize(size);
    return size;
}
#endif

} // namespace api
} // namespace bmalloc

BALLOW_UNSAFE_BUFFER_USAGE_END
