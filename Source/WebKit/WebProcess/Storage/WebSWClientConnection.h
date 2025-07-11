/*
 * Copyright (C) 2017-2025 Apple Inc. All rights reserved.
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

#include "Connection.h"
#include "MessageReceiver.h"
#include "MessageSender.h"
#include "RetrieveRecordResponseBodyCallbackIdentifier.h"
#include <WebCore/MessageWithMessagePorts.h>
#include <WebCore/SWClientConnection.h>
#include <WebCore/SharedMemory.h>
#include <wtf/Forward.h>
#include <wtf/UniqueRef.h>

namespace IPC {
class SharedBufferReference;
}

namespace WebCore {
struct CookieChangeSubscription;
struct ExceptionData;
class ResourceLoader;
}

namespace WebKit {

class WebSWOriginTable;
class WebServiceWorkerProvider;

class WebSWClientConnection final : public WebCore::SWClientConnection, private IPC::MessageSender, public IPC::MessageReceiver {
public:
    static Ref<WebSWClientConnection> create() { return adoptRef(*new WebSWClientConnection); }
    ~WebSWClientConnection();

    void ref() const final { WebCore::SWClientConnection::ref(); }
    void deref() const final { WebCore::SWClientConnection::deref(); }

    WebCore::SWServerConnectionIdentifier serverConnectionIdentifier() const final { return m_identifier; }

    void addServiceWorkerRegistrationInServer(WebCore::ServiceWorkerRegistrationIdentifier) final;
    void removeServiceWorkerRegistrationInServer(WebCore::ServiceWorkerRegistrationIdentifier) final;

    void disconnectedFromWebProcess();
    void didReceiveMessage(IPC::Connection&, IPC::Decoder&) final;

    bool mayHaveServiceWorkerRegisteredForOrigin(const WebCore::SecurityOriginData&) const final;

    void connectionToServerLost();

    bool isThrottleable() const { return m_isThrottleable; }
    void updateThrottleState();

    void terminateWorkerForTesting(WebCore::ServiceWorkerIdentifier, CompletionHandler<void()>&&);

private:
    WebSWClientConnection();

    void scheduleJobInServer(const WebCore::ServiceWorkerJobData&) final;
    void finishFetchingScriptInServer(const WebCore::ServiceWorkerJobDataIdentifier&, WebCore::ServiceWorkerRegistrationKey&&, WebCore::WorkerFetchResult&&) final;
    void postMessageToServiceWorker(WebCore::ServiceWorkerIdentifier destinationIdentifier, WebCore::MessageWithMessagePorts&&, const WebCore::ServiceWorkerOrClientIdentifier& source) final;
    void registerServiceWorkerClient(const WebCore::ClientOrigin&, WebCore::ServiceWorkerClientData&&, const std::optional<WebCore::ServiceWorkerRegistrationIdentifier>&, String&& userAgent) final;
    void unregisterServiceWorkerClient(WebCore::ScriptExecutionContextIdentifier) final;
    void scheduleUnregisterJobInServer(WebCore::ServiceWorkerRegistrationIdentifier, WebCore::ServiceWorkerOrClientIdentifier, CompletionHandler<void(WebCore::ExceptionOr<bool>&&)>&&) final;

    void matchRegistration(WebCore::SecurityOriginData&& topOrigin, const URL& clientURL, RegistrationCallback&&) final;
    void didMatchRegistration(uint64_t matchRequestIdentifier, std::optional<WebCore::ServiceWorkerRegistrationData>&&);
    void didGetRegistrations(uint64_t matchRequestIdentifier, Vector<WebCore::ServiceWorkerRegistrationData>&&);
    void whenRegistrationReady(const WebCore::SecurityOriginData& topOrigin, const URL& clientURL, WhenRegistrationReadyCallback&&) final;

    void setServiceWorkerClientIsControlled(WebCore::ScriptExecutionContextIdentifier, WebCore::ServiceWorkerRegistrationData&&, CompletionHandler<void(bool)>&&);
    void setWorkerIsControlled(WebCore::ScriptExecutionContextIdentifier, WebCore::ServiceWorkerRegistrationData&&, CompletionHandler<void(bool)>&&);

    void getRegistrations(WebCore::SecurityOriginData&& topOrigin, const URL& clientURL, GetRegistrationsCallback&&) final;
    void whenServiceWorkerIsTerminatedForTesting(WebCore::ServiceWorkerIdentifier, CompletionHandler<void()>&&) final;

    void didResolveRegistrationPromise(const WebCore::ServiceWorkerRegistrationKey&) final;
    void storeRegistrationsOnDiskForTesting(CompletionHandler<void()>&&) final;

    void subscribeToPushService(WebCore::ServiceWorkerRegistrationIdentifier, const Vector<uint8_t>& applicationServerKey, SubscribeToPushServiceCallback&&) final;
    void unsubscribeFromPushService(WebCore::ServiceWorkerRegistrationIdentifier, WebCore::PushSubscriptionIdentifier, UnsubscribeFromPushServiceCallback&&) final;
    void getPushSubscription(WebCore::ServiceWorkerRegistrationIdentifier, GetPushSubscriptionCallback&&) final;
    void getPushPermissionState(WebCore::ServiceWorkerRegistrationIdentifier, GetPushPermissionStateCallback&&) final;
#if ENABLE(NOTIFICATION_EVENT)
    void getNotifications(const URL&, const String&, GetNotificationsCallback&&) final;
#endif
    void enableNavigationPreload(WebCore::ServiceWorkerRegistrationIdentifier, ExceptionOrVoidCallback&&) final;
    void disableNavigationPreload(WebCore::ServiceWorkerRegistrationIdentifier, ExceptionOrVoidCallback&&) final;
    void setNavigationPreloadHeaderValue(WebCore::ServiceWorkerRegistrationIdentifier, String&&, ExceptionOrVoidCallback&&) final;
    void getNavigationPreloadState(WebCore::ServiceWorkerRegistrationIdentifier, ExceptionOrNavigationPreloadStateCallback&&) final;

    void startBackgroundFetch(WebCore::ServiceWorkerRegistrationIdentifier, const String&, Vector<WebCore::BackgroundFetchRequest>&&, WebCore::BackgroundFetchOptions&&, ExceptionOrBackgroundFetchInformationCallback&&) final;
    void backgroundFetchInformation(WebCore::ServiceWorkerRegistrationIdentifier, const String&, ExceptionOrBackgroundFetchInformationCallback&&) final;
    void backgroundFetchIdentifiers(WebCore::ServiceWorkerRegistrationIdentifier, BackgroundFetchIdentifiersCallback&&) final;
    void abortBackgroundFetch(WebCore::ServiceWorkerRegistrationIdentifier, const String&, AbortBackgroundFetchCallback&&) final;
    void matchBackgroundFetch(WebCore::ServiceWorkerRegistrationIdentifier, const String&, WebCore::RetrieveRecordsOptions&&, MatchBackgroundFetchCallback&&) final;
    void retrieveRecordResponse(WebCore::BackgroundFetchRecordIdentifier, RetrieveRecordResponseCallback&&) final;
    void retrieveRecordResponseBody(WebCore::BackgroundFetchRecordIdentifier, RetrieveRecordResponseBodyCallback&&) final;

    void addCookieChangeSubscriptions(WebCore::ServiceWorkerRegistrationIdentifier, Vector<WebCore::CookieChangeSubscription>&&, ExceptionOrVoidCallback&&) final;
    void removeCookieChangeSubscriptions(WebCore::ServiceWorkerRegistrationIdentifier, Vector<WebCore::CookieChangeSubscription>&&, ExceptionOrVoidCallback&&) final;
    void cookieChangeSubscriptions(WebCore::ServiceWorkerRegistrationIdentifier, ExceptionOrCookieChangeSubscriptionsCallback&&) final;
    Ref<AddRoutePromise> addRoutes(WebCore::ServiceWorkerRegistrationIdentifier, Vector<WebCore::ServiceWorkerRoute>&&) final;

    void focusServiceWorkerClient(WebCore::ScriptExecutionContextIdentifier, CompletionHandler<void(std::optional<WebCore::ServiceWorkerClientData>&&)>&&);
    void notifyRecordResponseBodyChunk(RetrieveRecordResponseBodyCallbackIdentifier, IPC::SharedBufferReference&&);
    void notifyRecordResponseBodyEnd(RetrieveRecordResponseBodyCallbackIdentifier, WebCore::ResourceError&&);

    void scheduleStorageJob(const WebCore::ServiceWorkerJobData&);

    void runOrDelayTaskForImport(Function<void()>&& task);

    IPC::Connection* messageSenderConnection() const final;
    uint64_t messageSenderDestinationID() const final { return 0; }

    void setSWOriginTableSharedMemory(WebCore::SharedMemory::Handle&&);
    void setSWOriginTableIsImported();

    void clear();

    WebCore::SWServerConnectionIdentifier m_identifier;

    const UniqueRef<WebSWOriginTable> m_swOriginTable;

    Deque<Function<void()>> m_tasksPendingOriginImport;
    bool m_isThrottleable { true };
    HashMap<RetrieveRecordResponseBodyCallbackIdentifier, RetrieveRecordResponseBodyCallback> m_retrieveRecordResponseBodyCallbacks;
};

} // namespace WebKit
