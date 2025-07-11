/*
 * Copyright (C) 2020 Apple Inc. All rights reserved.
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

#include "WebPageInspectorAgentBase.h"
#include <JavaScriptCore/InspectorBackendDispatchers.h>
#include <JavaScriptCore/InspectorFrontendDispatchers.h>
#include <wtf/CheckedPtr.h>
#include <wtf/Forward.h>
#include <wtf/TZoneMalloc.h>
#include <wtf/WeakRef.h>

namespace WebKit {

class WebPageProxy;

class InspectorBrowserAgent final : public InspectorAgentBase, public Inspector::BrowserBackendDispatcherHandler, public CanMakeCheckedPtr<InspectorBrowserAgent> {
    WTF_MAKE_NONCOPYABLE(InspectorBrowserAgent);
    WTF_MAKE_TZONE_ALLOCATED(InspectorBrowserAgent);
    WTF_OVERRIDE_DELETE_FOR_CHECKED_PTR(InspectorBrowserAgent);
public:
    InspectorBrowserAgent(WebPageAgentContext&);
    ~InspectorBrowserAgent();
    bool enabled() const;

    // InspectorAgentBase
    void didCreateFrontendAndBackend();
    void willDestroyFrontendAndBackend(Inspector::DisconnectReason);

    // BrowserBackendDispatcherHandler
    Inspector::Protocol::ErrorStringOr<void> enable();
    Inspector::Protocol::ErrorStringOr<void> disable();

    void extensionsEnabled(HashMap<String, String>&&);
    void extensionsDisabled(HashSet<String>&&);

private:
    const UniqueRef<Inspector::BrowserFrontendDispatcher> m_frontendDispatcher;
    const Ref<Inspector::BrowserBackendDispatcher> m_backendDispatcher;
    WeakRef<WebPageProxy> m_inspectedPage;
};

} // namespace WebKit
