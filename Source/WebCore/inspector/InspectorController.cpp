/*
 * Copyright (C) 2011 Google Inc. All rights reserved.
 * Copyright (C) 2014, 2015 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include "InspectorController.h"

#include "CommandLineAPIHost.h"
#include "CommonVM.h"
#include "DOMWrapperWorld.h"
#include "EventTargetInlines.h"
#include "GraphicsContext.h"
#include "InspectorAnimationAgent.h"
#include "InspectorBackendClient.h"
#include "InspectorCPUProfilerAgent.h"
#include "InspectorCSSAgent.h"
#include "InspectorDOMAgent.h"
#include "InspectorDOMStorageAgent.h"
#include "InspectorFrontendClient.h"
#include "InspectorIndexedDBAgent.h"
#include "InspectorInstrumentation.h"
#include "InspectorLayerTreeAgent.h"
#include "InspectorMemoryAgent.h"
#include "InspectorPageAgent.h"
#include "InstrumentingAgents.h"
#include "JSDOMBindingSecurity.h"
#include "JSDOMWindow.h"
#include "JSExecState.h"
#include "LocalDOMWindow.h"
#include "LocalFrame.h"
#include "Page.h"
#include "PageAuditAgent.h"
#include "PageCanvasAgent.h"
#include "PageConsoleAgent.h"
#include "PageDOMDebuggerAgent.h"
#include "PageDebugger.h"
#include "PageDebuggerAgent.h"
#include "PageHeapAgent.h"
#include "PageNetworkAgent.h"
#include "PageRuntimeAgent.h"
#include "PageTimelineAgent.h"
#include "PageWorkerAgent.h"
#include "Settings.h"
#include "SharedBuffer.h"
#include "WebInjectedScriptHost.h"
#include "WebInjectedScriptManager.h"
#include <JavaScriptCore/IdentifiersFactory.h>
#include <JavaScriptCore/InspectorAgent.h>
#include <JavaScriptCore/InspectorBackendDispatcher.h>
#include <JavaScriptCore/InspectorBackendDispatchers.h>
#include <JavaScriptCore/InspectorFrontendDispatchers.h>
#include <JavaScriptCore/InspectorFrontendRouter.h>
#include <JavaScriptCore/InspectorScriptProfilerAgent.h>
#include <JavaScriptCore/JSLock.h>
#include <wtf/Stopwatch.h>
#include <wtf/TZoneMallocInlines.h>

#if ENABLE(REMOTE_INSPECTOR)
#include "PageDebuggable.h"
#endif

namespace WebCore {

using namespace JSC;
using namespace Inspector;

WTF_MAKE_TZONE_ALLOCATED_IMPL(InspectorController);

InspectorController::InspectorController(Page& page, std::unique_ptr<InspectorBackendClient>&& inspectorBackendClient)
    : m_page(page)
    , m_instrumentingAgents(InstrumentingAgents::create(*this))
    , m_injectedScriptManager(makeUniqueRef<WebInjectedScriptManager>(*this, WebInjectedScriptHost::create()))
    , m_frontendRouter(FrontendRouter::create())
    , m_backendDispatcher(BackendDispatcher::create(m_frontendRouter.copyRef()))
    , m_overlay(makeUniqueRefWithoutRefCountedCheck<InspectorOverlay>(*this, inspectorBackendClient.get()))
    , m_executionStopwatch(Stopwatch::create())
    , m_inspectorBackendClient(WTFMove(inspectorBackendClient))
{
    ASSERT_ARG(inspectorBackendClient, m_inspectorBackendClient);

    auto pageContext = pageAgentContext();

    auto consoleAgent = makeUnique<PageConsoleAgent>(pageContext);
    m_instrumentingAgents->setWebConsoleAgent(consoleAgent.get());
    m_agents.append(WTFMove(consoleAgent));
}

InspectorController::~InspectorController()
{
    m_instrumentingAgents->reset();
    ASSERT(!m_inspectorBackendClient);
}

void InspectorController::ref() const
{
    m_page->ref();
}

void InspectorController::deref() const
{
    m_page->deref();
}

PageAgentContext InspectorController::pageAgentContext()
{
    AgentContext baseContext = {
        *this,
        m_injectedScriptManager,
        m_frontendRouter,
        m_backendDispatcher
    };

    WebAgentContext webContext = {
        baseContext,
        m_instrumentingAgents.get()
    };

    PageAgentContext pageContext = {
        webContext,
        m_page
    };

    return pageContext;
}

void InspectorController::createLazyAgents()
{
    if (m_didCreateLazyAgents)
        return;

    m_didCreateLazyAgents = true;

    m_debugger = makeUnique<PageDebugger>(m_page);

    m_injectedScriptManager->connect();

    auto pageContext = pageAgentContext();

    ensureInspectorAgent();
    ensurePageAgent();

    m_agents.append(makeUnique<PageRuntimeAgent>(pageContext));

    auto debuggerAgent = makeUnique<PageDebuggerAgent>(pageContext);
    auto debuggerAgentPtr = debuggerAgent.get();
    m_agents.append(WTFMove(debuggerAgent));

    m_agents.append(makeUnique<PageNetworkAgent>(pageContext, m_inspectorBackendClient.get()));
    m_agents.append(makeUnique<InspectorCSSAgent>(pageContext));
    ensureDOMAgent();
    m_agents.append(makeUnique<PageDOMDebuggerAgent>(pageContext, debuggerAgentPtr));
    m_agents.append(makeUnique<InspectorLayerTreeAgent>(pageContext));
    m_agents.append(makeUnique<PageWorkerAgent>(pageContext));
    m_agents.append(makeUnique<InspectorDOMStorageAgent>(pageContext));
    m_agents.append(makeUnique<InspectorIndexedDBAgent>(pageContext));

    auto scriptProfilerAgentPtr = makeUnique<InspectorScriptProfilerAgent>(pageContext);
    m_instrumentingAgents->setPersistentScriptProfilerAgent(scriptProfilerAgentPtr.get());
    m_agents.append(WTFMove(scriptProfilerAgentPtr));

#if ENABLE(RESOURCE_USAGE)
    m_agents.append(makeUnique<InspectorCPUProfilerAgent>(pageContext));
    m_agents.append(makeUnique<InspectorMemoryAgent>(pageContext));
#endif
    m_agents.append(makeUnique<PageHeapAgent>(pageContext));
    m_agents.append(makeUnique<PageAuditAgent>(pageContext));
    m_agents.append(makeUnique<PageCanvasAgent>(pageContext));
    m_agents.append(makeUnique<PageTimelineAgent>(pageContext));
    m_agents.append(makeUnique<InspectorAnimationAgent>(pageContext));

    if (auto& commandLineAPIHost = m_injectedScriptManager->commandLineAPIHost())
        commandLineAPIHost->init(m_instrumentingAgents.copyRef());
}

void InspectorController::inspectedPageDestroyed()
{
    // Clean up resources and disconnect local and remote frontends.
    disconnectAllFrontends();

    // Disconnect the client.
    m_inspectorBackendClient->inspectedPageDestroyed();
    m_inspectorBackendClient = nullptr;

    m_agents.discardValues();

    m_debugger = nullptr;
}

void InspectorController::setInspectorFrontendClient(InspectorFrontendClient* inspectorFrontendClient)
{
    m_inspectorFrontendClient = inspectorFrontendClient;
}

bool InspectorController::hasLocalFrontend() const
{
    return m_frontendRouter->hasLocalFrontend();
}

bool InspectorController::hasRemoteFrontend() const
{
    return m_frontendRouter->hasRemoteFrontend();
}

unsigned InspectorController::inspectionLevel() const
{
    return m_inspectorFrontendClient ? m_inspectorFrontendClient->inspectionLevel() : 0;
}

void InspectorController::didClearWindowObjectInWorld(LocalFrame& frame, DOMWrapperWorld& world)
{
    if (&world != &mainThreadNormalWorldSingleton())
        return;

    if (frame.isMainFrame())
        m_injectedScriptManager->discardInjectedScripts();

    // If the page is supposed to serve as InspectorFrontend notify inspector frontend
    // client that it's cleared so that the client can expose inspector bindings.
    if (m_inspectorFrontendClient && frame.isMainFrame())
        m_inspectorFrontendClient->windowObjectCleared();
}

void InspectorController::connectFrontend(Inspector::FrontendChannel& frontendChannel, bool isAutomaticInspection, bool immediatelyPause)
{
    ASSERT(m_inspectorBackendClient);

    // If a frontend has connected enable the developer extras and keep them enabled.
    m_page->settings().setDeveloperExtrasEnabled(true);

    createLazyAgents();

    bool connectedFirstFrontend = !m_frontendRouter->hasFrontends();
    m_isAutomaticInspection = isAutomaticInspection;
    m_pauseAfterInitialization = immediatelyPause;

    m_frontendRouter->connectFrontend(frontendChannel);

    InspectorInstrumentation::frontendCreated();

    if (connectedFirstFrontend) {
        InspectorInstrumentation::registerInstrumentingAgents(m_instrumentingAgents.get());
        m_agents.didCreateFrontendAndBackend();
    }

    m_inspectorBackendClient->frontendCountChanged(m_frontendRouter->frontendCount());

#if ENABLE(REMOTE_INSPECTOR)
    if (hasLocalFrontend())
        m_page->remoteInspectorInformationDidChange();
#endif
}

void InspectorController::disconnectFrontend(FrontendChannel& frontendChannel)
{
    m_frontendRouter->disconnectFrontend(frontendChannel);

    m_isAutomaticInspection = false;
    m_pauseAfterInitialization = false;

    InspectorInstrumentation::frontendDeleted();

    bool disconnectedLastFrontend = !m_frontendRouter->hasFrontends();
    if (disconnectedLastFrontend) {
        // Notify agents first.
        m_agents.willDestroyFrontendAndBackend(DisconnectReason::InspectorDestroyed);

        // Clean up inspector resources.
        m_injectedScriptManager->discardInjectedScripts();

        // Unplug all instrumentations since they aren't needed now.
        InspectorInstrumentation::unregisterInstrumentingAgents(m_instrumentingAgents.get());
    }

    m_inspectorBackendClient->frontendCountChanged(m_frontendRouter->frontendCount());

#if ENABLE(REMOTE_INSPECTOR)
    if (disconnectedLastFrontend)
        m_page->remoteInspectorInformationDidChange();
#endif
}

void InspectorController::disconnectAllFrontends()
{
    // If the local frontend page was destroyed, close the window.
    if (m_inspectorFrontendClient)
        m_inspectorFrontendClient->closeWindow();

    // The frontend should call setInspectorFrontendClient(nullptr) under closeWindow().
    ASSERT(!m_inspectorFrontendClient);

    if (!m_frontendRouter->hasFrontends())
        return;

    for (unsigned i = 0; i < m_frontendRouter->frontendCount(); ++i)
        InspectorInstrumentation::frontendDeleted();

    // Unplug all instrumentations to prevent further agent callbacks.
    InspectorInstrumentation::unregisterInstrumentingAgents(m_instrumentingAgents.get());

    // Notify agents first, since they may need to use InspectorBackendClient.
    m_agents.willDestroyFrontendAndBackend(DisconnectReason::InspectedTargetDestroyed);

    // Clean up inspector resources.
    m_injectedScriptManager->disconnect();

    // Disconnect any remaining remote frontends.
    m_frontendRouter->disconnectAllFrontends();
    m_isAutomaticInspection = false;
    m_pauseAfterInitialization = false;

    m_inspectorBackendClient->frontendCountChanged(m_frontendRouter->frontendCount());

#if ENABLE(REMOTE_INSPECTOR)
    m_page->remoteInspectorInformationDidChange();
#endif
}

void InspectorController::show()
{
    ASSERT(!hasRemoteFrontend());

    if (!enabled())
        return;

    if (m_frontendRouter->hasLocalFrontend())
        m_inspectorBackendClient->bringFrontendToFront();
    else if (Inspector::FrontendChannel* frontendChannel = m_inspectorBackendClient->openLocalFrontend(this))
        connectFrontend(*frontendChannel);
}

void InspectorController::evaluateForTestInFrontend(const String& script)
{
    ensureInspectorAgent().evaluateForTestInFrontend(script);
}

void InspectorController::drawHighlight(GraphicsContext& context) const
{
    m_overlay->paint(context);
}

void InspectorController::getHighlight(InspectorOverlay::Highlight& highlight, InspectorOverlay::CoordinateSystem coordinateSystem) const
{
    m_overlay->getHighlight(highlight, coordinateSystem);
}

unsigned InspectorController::gridOverlayCount() const
{
    return m_overlay->gridOverlayCount();
}

unsigned InspectorController::flexOverlayCount() const
{
    return m_overlay->flexOverlayCount();
}

unsigned InspectorController::paintRectCount() const
{
    if (m_inspectorBackendClient->overridesShowPaintRects())
        return m_inspectorBackendClient->paintRectCount();

    return m_overlay->paintRectCount();
}

bool InspectorController::shouldShowOverlay() const
{
    return m_overlay->shouldShowOverlay();
}

void InspectorController::inspect(Node* node)
{
    if (!enabled())
        return;

    if (!hasRemoteFrontend())
        show();

    ensureDOMAgent().inspect(node);
}

bool InspectorController::enabled() const
{
    // FIXME: <http://webkit.org/b/246237> Local inspection should be controlled by `inspectable` API.
    return developerExtrasEnabled();
}

Page& InspectorController::inspectedPage() const
{
    return m_page;
}

void InspectorController::dispatchMessageFromFrontend(const String& message)
{
    m_backendDispatcher->dispatch(message);
}

void InspectorController::hideHighlight()
{
    m_overlay->hideHighlight();
}

Node* InspectorController::highlightedNode() const
{
    return m_overlay->highlightedNode();
}

void InspectorController::setIndicating(bool indicating)
{
#if !PLATFORM(IOS_FAMILY)
    m_overlay->setIndicating(indicating);
#else
    if (indicating)
        m_inspectorBackendClient->showInspectorIndication();
    else
        m_inspectorBackendClient->hideInspectorIndication();
#endif
}

InspectorAgent& InspectorController::ensureInspectorAgent()
{
    if (!m_inspectorAgent) {
        auto pageContext = pageAgentContext();
        auto inspectorAgent = makeUnique<InspectorAgent>(pageContext);
        m_inspectorAgent = inspectorAgent.get();
        m_instrumentingAgents->setPersistentInspectorAgent(m_inspectorAgent);
        m_agents.append(WTFMove(inspectorAgent));
    }
    return *m_inspectorAgent;
}

InspectorDOMAgent& InspectorController::ensureDOMAgent()
{
    if (!m_domAgent) {
        auto pageContext = pageAgentContext();
        auto domAgent = makeUnique<InspectorDOMAgent>(pageContext, m_overlay.get());
        m_domAgent = domAgent.get();
        m_agents.append(WTFMove(domAgent));
    }
    return *m_domAgent;
}

InspectorPageAgent& InspectorController::ensurePageAgent()
{
    if (!m_pageAgent) {
        auto pageContext = pageAgentContext();
        auto pageAgent = makeUnique<InspectorPageAgent>(pageContext, m_inspectorBackendClient.get(), m_overlay.get());
        m_pageAgent = pageAgent.get();
        m_agents.append(WTFMove(pageAgent));
    }
    return *m_pageAgent;
}

bool InspectorController::developerExtrasEnabled() const
{
    return m_page->settings().developerExtrasEnabled();
}

bool InspectorController::canAccessInspectedScriptState(JSC::JSGlobalObject* lexicalGlobalObject) const
{
    JSLockHolder lock(lexicalGlobalObject);

    auto* inspectedWindow = jsDynamicCast<JSDOMWindow*>(lexicalGlobalObject);
    if (!inspectedWindow)
        return false;

    return BindingSecurity::shouldAllowAccessToDOMWindow(lexicalGlobalObject, inspectedWindow->wrapped(), DoNotReportSecurityError);
}

InspectorFunctionCallHandler InspectorController::functionCallHandler() const
{
    return WebCore::functionCallHandlerFromAnyThread;
}

InspectorEvaluateHandler InspectorController::evaluateHandler() const
{
    return WebCore::evaluateHandlerFromAnyThread;
}

void InspectorController::frontendInitialized()
{
    if (m_pauseAfterInitialization) {
        m_pauseAfterInitialization = false;
        if (auto* debuggerAgent = m_instrumentingAgents->enabledPageDebuggerAgent())
            debuggerAgent->pause();
    }

#if ENABLE(REMOTE_INSPECTOR)
    if (m_isAutomaticInspection)
        m_page->inspectorDebuggable().unpauseForResolvedAutomaticInspection();
#endif
}

Stopwatch& InspectorController::executionStopwatch() const
{
    return m_executionStopwatch;
}

JSC::Debugger* InspectorController::debugger()
{
    ASSERT_IMPLIES(m_didCreateLazyAgents, m_debugger);
    return m_debugger.get();
}

JSC::VM& InspectorController::vm()
{
    return commonVM();
}

void InspectorController::willComposite(LocalFrame& frame)
{
    InspectorInstrumentation::willComposite(frame);
}

void InspectorController::didComposite(LocalFrame& frame)
{
    InspectorInstrumentation::didComposite(frame);
}

} // namespace WebCore
