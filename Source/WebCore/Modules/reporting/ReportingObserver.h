/*
 * Copyright (C) 2022-2025 Apple Inc. All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#pragma once

#include "ActiveDOMObject.h"
#include <wtf/RefCounted.h>
#include <wtf/TZoneMalloc.h>
#include <wtf/WeakPtr.h>

namespace WebCore {

class Report;
class ReportingObserverCallback;
class ReportingScope;
class ScriptExecutionContext;

class ReportingObserver final : public RefCounted<ReportingObserver>, public ActiveDOMObject  {
    WTF_MAKE_TZONE_OR_ISO_ALLOCATED(ReportingObserver);
public:
    void ref() const final { RefCounted::ref(); }
    void deref() const final { RefCounted::deref(); }

    struct Options {
        std::optional<Vector<AtomString>> types;
        bool buffered { false };
    };

    static Ref<ReportingObserver> create(ScriptExecutionContext&, Ref<ReportingObserverCallback>&&, ReportingObserver::Options&&);

    virtual ~ReportingObserver();

    void observe();
    void disconnect();
    Vector<Ref<Report>> takeRecords();

    void appendQueuedReportIfCorrectType(const Ref<Report>&);

    ReportingObserverCallback& callbackConcurrently();

    bool virtualHasPendingActivity() const final;

private:
    explicit ReportingObserver(ScriptExecutionContext&, Ref<ReportingObserverCallback>&&, ReportingObserver::Options&&);

    WeakPtr<ReportingScope> m_reportingScope;
    const Ref<ReportingObserverCallback> m_callback;
    // Instead of storing an Options struct we store the fields separately to save the space overhead of an optional<Vector<AtomString>>
    // which is logically equivalent to an empty vector by the spec.
    const Vector<AtomString> m_types;
    bool m_buffered;
    Vector<Ref<Report>> m_queuedReports;
};

}
