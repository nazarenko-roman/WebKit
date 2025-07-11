/*
 * Copyright (C) 2008-2021 Apple Inc. All rights reserved.
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
 *
 */

#pragma once

#include <wtf/TZoneMalloc.h>
#include <wtf/ThreadSafeRefCounted.h>
#include <wtf/Threading.h>
#include <wtf/text/StringHash.h>

namespace PAL {

struct ICUConverterWrapper;

class ThreadGlobalData : public WTF::Thread::ClientData {
    WTF_MAKE_TZONE_ALLOCATED(ThreadGlobalData);
    WTF_MAKE_NONCOPYABLE(ThreadGlobalData);
public:
    PAL_EXPORT virtual ~ThreadGlobalData();

    ICUConverterWrapper& cachedConverterICU() { return m_cachedConverterICU; }

protected:
    PAL_EXPORT ThreadGlobalData();

private:
    PAL_EXPORT friend ThreadGlobalData& threadGlobalData();

    const UniqueRef<ICUConverterWrapper> m_cachedConverterICU;
};

#if USE(WEB_THREAD)
PAL_EXPORT ThreadGlobalData& threadGlobalData();
#else
PAL_EXPORT ThreadGlobalData& threadGlobalData() PURE_FUNCTION;
#endif

} // namespace PAL
