/*
 * Copyright (C) 2009 Apple Inc. All rights reserved.
 * Copyright (C) 2012 Igalia S.L.
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
 * THIS SOFTWARE IS PROVIDED BY APPLE AND ITS CONTRIBUTORS "AS IS" AND ANY
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

#include "DNS.h"
#include "Timer.h"
#include <atomic>
#include <wtf/Forward.h>
#include <wtf/HashSet.h>
#include <wtf/text/StringHash.h>

namespace WebCore {

class DNSResolveQueue {
    friend NeverDestroyed<DNSResolveQueue>;

public:
    virtual ~DNSResolveQueue() = default;

    static DNSResolveQueue& singleton();

    // Do nothing since this is a singleton.
    void ref() const { }
    void deref() const { }

    virtual void resolve(const String& hostname, uint64_t identifier, DNSCompletionHandler&&) = 0;
    virtual void stopResolve(uint64_t identifier) = 0;
    void add(const String& hostname);
    void decrementRequestCount()
    {
        --m_requestsInFlight;
    }

protected:
    DNSResolveQueue();
    bool isUsingProxy();

    bool m_isUsingProxy { true };

private:
    virtual void updateIsUsingProxy() = 0;
    virtual void platformResolve(const String&) = 0;
    void timerFired();

    Timer m_timer;

    HashSet<String> m_names;
    std::atomic<int> m_requestsInFlight;
    MonotonicTime m_lastProxyEnabledStatusCheckTime;
};

}
