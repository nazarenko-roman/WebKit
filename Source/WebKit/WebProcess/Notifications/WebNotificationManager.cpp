/*
 * Copyright (C) 2011-2025 Apple Inc. All rights reserved.
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

#include "config.h"
#include "WebNotificationManager.h"

#include "Logging.h"
#include "WebPage.h"
#include "WebProcess.h"
#include "WebProcessCreationParameters.h"
#include <wtf/TZoneMallocInlines.h>

#if ENABLE(NOTIFICATIONS)
#include "NetworkProcessConnection.h"
#include "NotificationManagerMessageHandlerMessages.h"
#include "ServiceWorkerNotificationHandler.h"
#include "WebNotification.h"
#include "WebNotificationManagerMessages.h"
#include "WebPageProxyMessages.h"
#include <WebCore/DeprecatedGlobalSettings.h>
#include <WebCore/Document.h>
#include <WebCore/Notification.h>
#include <WebCore/NotificationData.h>
#include <WebCore/Page.h>
#include <WebCore/PushPermissionState.h>
#include <WebCore/SWContextManager.h>
#include <WebCore/ScriptExecutionContext.h>
#include <WebCore/SecurityOrigin.h>
#include <WebCore/Settings.h>
#include <WebCore/UserGestureIndicator.h>
#endif

namespace WebKit {
using namespace WebCore;

#if ENABLE(NOTIFICATIONS)
static bool sendMessage(WebPage* page, NOESCAPE const Function<bool(IPC::Connection&, uint64_t)>& sendMessage)
{
#if ENABLE(WEB_PUSH_NOTIFICATIONS)
    if (DeprecatedGlobalSettings::builtInNotificationsEnabled()) {
        Ref networkProcessConnection = WebProcess::singleton().ensureNetworkProcessConnection().connection();
        return sendMessage(networkProcessConnection, WebProcess::singleton().sessionID().toUInt64());
    }
#endif

    std::optional<WebCore::PageIdentifier> pageIdentifier;
    if (page)
        pageIdentifier = page->identifier();
    else if (RefPtr connection = SWContextManager::singleton().connection()) {
        // Pageless notification messages are, by default, on behalf of a service worker.
        // So use the service worker connection's page identifier.
        pageIdentifier = connection->pageIdentifier();
    }

    ASSERT(pageIdentifier);
    Ref parentConnection = *WebProcess::singleton().parentProcessConnection();
    return sendMessage(parentConnection, pageIdentifier->toUInt64());
}

template<typename U> static bool sendNotificationMessage(U&& message, WebPage* page)
{
    return sendMessage(page, [&] (auto& connection, auto destinationIdentifier) {
        return connection.send(std::forward<U>(message), destinationIdentifier) == IPC::Error::NoError;
    });
}

template<typename U, typename C>
static bool sendNotificationMessageWithAsyncReply(U&& message, WebPage* page, C&& callback)
{
    return sendMessage(page, [&] (auto& connection, auto destinationIdentifier) {
        return !!connection.sendWithAsyncReply(std::forward<U>(message), WTFMove(callback), destinationIdentifier);
    });
}
#endif // ENABLE(NOTIFICATIONS)

WTF_MAKE_TZONE_ALLOCATED_IMPL(WebNotificationManager);

ASCIILiteral WebNotificationManager::supplementName()
{
    return "WebNotificationManager"_s;
}

WebNotificationManager::WebNotificationManager(WebProcess& process)
    : m_process(process)
{
#if ENABLE(NOTIFICATIONS)
    process.addMessageReceiver(Messages::WebNotificationManager::messageReceiverName(), *this);
#endif
}

WebNotificationManager::~WebNotificationManager() = default;

void WebNotificationManager::ref() const
{
    m_process->ref();
}

void WebNotificationManager::deref() const
{
    m_process->deref();
}

void WebNotificationManager::initialize(const WebProcessCreationParameters& parameters)
{
#if ENABLE(NOTIFICATIONS)
    m_permissionsMap = parameters.notificationPermissions;
#else
    UNUSED_PARAM(parameters);
#endif
}

void WebNotificationManager::didUpdateNotificationDecision(const String& originString, bool allowed)
{
#if ENABLE(NOTIFICATIONS)
    if (!originString.isEmpty())
        m_permissionsMap.set(originString, allowed);
#else
    UNUSED_PARAM(originString);
    UNUSED_PARAM(allowed);
#endif
}

void WebNotificationManager::didRemoveNotificationDecisions(const Vector<String>& originStrings)
{
#if ENABLE(NOTIFICATIONS)
    for (auto& originString : originStrings) {
        if (!originString.isEmpty())
            m_permissionsMap.remove(originString);
    }
#else
    UNUSED_PARAM(originStrings);
#endif
}

NotificationClient::Permission WebNotificationManager::policyForOrigin(const String& originString, WebPage* page) const
{
#if ENABLE(WEB_PUSH_NOTIFICATIONS) && ENABLE(NOTIFICATIONS)
    if (DeprecatedGlobalSettings::builtInNotificationsEnabled()) {
        Ref connection = WebProcess::singleton().ensureNetworkProcessConnection().connection();
        auto origin = SecurityOriginData::fromURL(URL { originString });
        auto result = connection->sendSync(Messages::NotificationManagerMessageHandler::GetPermissionStateSync(WTFMove(origin)), WebProcess::singleton().sessionID().toUInt64());
        if (!result.succeeded())
            RELEASE_LOG_ERROR(Notifications, "Could not look up notification permission for origin %" SENSITIVE_LOG_STRING": %u", originString.utf8().data(), static_cast<unsigned>(result.error()));

        auto [pushPermission] = result.takeReplyOr(PushPermissionState::Denied);
        switch (pushPermission) {
        case PushPermissionState::Denied:
            return NotificationPermission::Denied;
        case PushPermissionState::Granted:
            return NotificationPermission::Granted;
        case PushPermissionState::Prompt:
            return NotificationPermission::Default;
        default:
            RELEASE_ASSERT_NOT_REACHED();
        }
    }
#endif // ENABLE(WEB_PUSH_NOTIFICATIONS) && ENABLE(NOTIFICATIONS)

#if ENABLE(NOTIFICATIONS)
    if (originString.isEmpty())
        return NotificationClient::Permission::Default;

    auto it = m_permissionsMap.find(originString);
    if (it != m_permissionsMap.end()) {
        if (it->value && page)
            sendNotificationMessage(Messages::NotificationManagerMessageHandler::PageWasNotifiedOfNotificationPermission(), page);
        return it->value ? NotificationClient::Permission::Granted : NotificationClient::Permission::Denied;
    }
#else
    UNUSED_PARAM(originString);
#endif
    
    return NotificationClient::Permission::Default;
}

void WebNotificationManager::removeAllPermissionsForTesting()
{
#if ENABLE(NOTIFICATIONS)
    m_permissionsMap.clear();
#endif
}

bool WebNotificationManager::show(NotificationData&& notification, RefPtr<NotificationResources>&& resources, WebPage* page, CompletionHandler<void()>&& callback)
{
#if ENABLE(NOTIFICATIONS)
    auto notificationID = notification.notificationID;
    LOG(Notifications, "WebProcess %i going to show notification %s", getpid(), notificationID.toString().utf8().data());

    ASSERT(isMainRunLoop());
    if (page && !page->corePage()->settings().notificationsEnabled()) {
        callback();
        return false;
    }

    if (!sendNotificationMessageWithAsyncReply(Messages::NotificationManagerMessageHandler::ShowNotification(notification, resources), page, WTFMove(callback)))
        return false;

    if (!notification.isPersistent()) {
        ASSERT(!m_nonPersistentNotificationsContexts.contains(notificationID));
        RELEASE_ASSERT(notification.contextIdentifier);
        m_nonPersistentNotificationsContexts.add(notificationID, *notification.contextIdentifier);
    }
    return true;
#else
    UNUSED_PARAM(notification);
    UNUSED_PARAM(resources);
    UNUSED_PARAM(page);
    return false;
#endif
}

void WebNotificationManager::cancel(NotificationData&& notification, WebPage* page)
{
    ASSERT(isMainRunLoop());

#if ENABLE(NOTIFICATIONS)
    auto identifier = notification.notificationID;
    ASSERT(notification.isPersistent() || m_nonPersistentNotificationsContexts.contains(identifier));

    auto origin = WebCore::SecurityOriginData::fromURL(URL { notification.originString });
    if (!sendNotificationMessage(Messages::NotificationManagerMessageHandler::CancelNotification(WTFMove(origin), identifier), page))
        return;
#else
    UNUSED_PARAM(notification);
    UNUSED_PARAM(page);
#endif
}

void WebNotificationManager::requestPermission(WebCore::SecurityOriginData&& origin, RefPtr<WebPage> page, CompletionHandler<void(bool)>&& callback)
{
    ASSERT(isMainRunLoop());

#if ENABLE(NOTIFICATIONS)
    sendNotificationMessageWithAsyncReply(Messages::NotificationManagerMessageHandler::RequestPermission(WTFMove(origin)), page.get(), WTFMove(callback));
#else
    UNUSED_PARAM(origin);
    callback(false);
#endif
}

void WebNotificationManager::didDestroyNotification(NotificationData&& notification, WebPage* page)
{
    ASSERT(isMainRunLoop());

#if ENABLE(NOTIFICATIONS)
    auto identifier = notification.notificationID;
    if (!notification.isPersistent())
        m_nonPersistentNotificationsContexts.remove(identifier);

    sendNotificationMessage(Messages::NotificationManagerMessageHandler::DidDestroyNotification(identifier), page);
#else
    UNUSED_PARAM(notification);
    UNUSED_PARAM(page);
#endif
}

void WebNotificationManager::didShowNotification(const WTF::UUID& notificationID)
{
    ASSERT(isMainRunLoop());

    LOG(Notifications, "WebProcess %i DID SHOW notification %s", getpid(), notificationID.toString().utf8().data());

#if ENABLE(NOTIFICATIONS)
    auto contextIdentifier = m_nonPersistentNotificationsContexts.get(notificationID);
    if (!contextIdentifier)
        return;

    Notification::ensureOnNotificationThread(contextIdentifier, notificationID, [](auto* notification) {
        if (notification)
            notification->dispatchShowEvent();
    });
#else
    UNUSED_PARAM(notificationID);
#endif
}

void WebNotificationManager::didClickNotification(const WTF::UUID& notificationID)
{
    ASSERT(isMainRunLoop());

    LOG(Notifications, "WebProcess %i DID CLICK notification %s", getpid(), notificationID.toString().utf8().data());

#if ENABLE(NOTIFICATIONS)
    auto contextIdentifier = m_nonPersistentNotificationsContexts.get(notificationID);
    if (!contextIdentifier)
        return;

    LOG(Notifications, "WebProcess %i handling click event for notification %s", getpid(), notificationID.toString().utf8().data());

    Notification::ensureOnNotificationThread(contextIdentifier, notificationID, [](auto* notification) {
        if (!notification)
            return;

        // Indicate that this event is being dispatched in reaction to a user's interaction with a platform notification.
        if (isMainRunLoop()) {
            UserGestureIndicator indicator(IsProcessingUserGesture::Yes);
            notification->dispatchClickEvent();
        } else
            notification->dispatchClickEvent();
    });
#else
    UNUSED_PARAM(notificationID);
#endif
}

void WebNotificationManager::didCloseNotifications(const Vector<WTF::UUID>& notificationIDs)
{
    ASSERT(isMainRunLoop());

#if ENABLE(NOTIFICATIONS)
    for (auto& notificationID : notificationIDs) {
        auto contextIdentifier = m_nonPersistentNotificationsContexts.get(notificationID);
        if (!contextIdentifier)
            continue;

        Notification::ensureOnNotificationThread(contextIdentifier, notificationID, [](auto* notification) {
            if (notification)
                notification->dispatchCloseEvent();
        });
    }
#else
    UNUSED_PARAM(notificationIDs);
#endif
}

} // namespace WebKit
