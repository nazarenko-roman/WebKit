/*
 * Copyright (C) 2015-2019 Apple Inc. All rights reserved.
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
#include "WebPaymentCoordinator.h"

#if ENABLE(APPLE_PAY)

#include "ApplePayPaymentSetupFeaturesWebKit.h"
#include "MessageSenderInlines.h"
#include "PaymentSetupConfigurationWebKit.h"
#include "WebPage.h"
#include "WebPaymentCoordinatorMessages.h"
#include "WebPaymentCoordinatorProxyMessages.h"
#include "WebProcess.h"
#include <WebCore/ApplePayCouponCodeUpdate.h>
#include <WebCore/ApplePayPaymentAuthorizationResult.h>
#include <WebCore/ApplePayPaymentMethodUpdate.h>
#include <WebCore/ApplePayShippingContactUpdate.h>
#include <WebCore/ApplePayShippingMethodUpdate.h>
#include <WebCore/LocalFrame.h>
#include <WebCore/PaymentCoordinator.h>
#include <wtf/TZoneMallocInlines.h>
#include <wtf/URL.h>

namespace WebKit {

WTF_MAKE_TZONE_ALLOCATED_IMPL(WebPaymentCoordinator);

Ref<WebPaymentCoordinator> WebPaymentCoordinator::create(WebPage& webPage)
{
    return adoptRef(*new WebPaymentCoordinator(webPage));
}

WebPaymentCoordinator::WebPaymentCoordinator(WebPage& webPage)
    : m_webPage(webPage)
{
    WebProcess::singleton().addMessageReceiver(Messages::WebPaymentCoordinator::messageReceiverName(), webPage.identifier(), *this);
}

WebPaymentCoordinator::~WebPaymentCoordinator()
{
    WebProcess::singleton().removeMessageReceiver(*this);
}

void WebPaymentCoordinator::networkProcessConnectionClosed()
{
#if ENABLE(APPLE_PAY_REMOTE_UI)
    didCancelPaymentSession({ });
#endif
}

std::optional<String> WebPaymentCoordinator::validatedPaymentNetwork(const String& paymentNetwork) const
{
    if (!m_availablePaymentNetworks)
        m_availablePaymentNetworks = platformAvailablePaymentNetworks();

    auto result = m_availablePaymentNetworks->find(paymentNetwork);
    if (result == m_availablePaymentNetworks->end())
        return std::nullopt;
    return *result;
}

bool WebPaymentCoordinator::canMakePayments()
{
    auto now = MonotonicTime::now();
    if (now - m_timestampOfLastCanMakePaymentsRequest > 1_min || !m_lastCanMakePaymentsResult) {
        auto sendResult = sendSync(Messages::WebPaymentCoordinatorProxy::CanMakePayments());
        if (!sendResult.succeeded())
            return false;
        auto [canMakePayments] = sendResult.takeReply();

        m_timestampOfLastCanMakePaymentsRequest = now;
        m_lastCanMakePaymentsResult = canMakePayments;
    }
    return *m_lastCanMakePaymentsResult;
}

void WebPaymentCoordinator::canMakePaymentsWithActiveCard(const String& merchantIdentifier, const String& domainName, CompletionHandler<void(bool)>&& completionHandler)
{
    sendWithAsyncReply(Messages::WebPaymentCoordinatorProxy::CanMakePaymentsWithActiveCard(merchantIdentifier, domainName), WTFMove(completionHandler));
}

void WebPaymentCoordinator::openPaymentSetup(const String& merchantIdentifier, const String& domainName, CompletionHandler<void(bool)>&& completionHandler)
{
    sendWithAsyncReply(Messages::WebPaymentCoordinatorProxy::OpenPaymentSetup(merchantIdentifier, domainName), WTFMove(completionHandler));
}

bool WebPaymentCoordinator::showPaymentUI(const URL& originatingURL, const Vector<URL>& linkIconURLs, const WebCore::ApplePaySessionPaymentRequest& paymentRequest)
{
    RefPtr webPage = m_webPage.get();
    if (!webPage)
        return false;

    auto sendResult = sendSync(Messages::WebPaymentCoordinatorProxy::ShowPaymentUI(webPage->identifier(), webPage->webPageProxyIdentifier(), originatingURL, linkIconURLs, paymentRequest));
    auto [result] = sendResult.takeReplyOr(false);
    return result;
}

void WebPaymentCoordinator::completeMerchantValidation(const WebCore::PaymentMerchantSession& paymentMerchantSession)
{
    send(Messages::WebPaymentCoordinatorProxy::CompleteMerchantValidation(paymentMerchantSession));
}

void WebPaymentCoordinator::completeShippingMethodSelection(std::optional<WebCore::ApplePayShippingMethodUpdate>&& update)
{
    send(Messages::WebPaymentCoordinatorProxy::CompleteShippingMethodSelection(WTFMove(update)));
}

void WebPaymentCoordinator::completeShippingContactSelection(std::optional<WebCore::ApplePayShippingContactUpdate>&& update)
{
    send(Messages::WebPaymentCoordinatorProxy::CompleteShippingContactSelection(WTFMove(update)));
}

void WebPaymentCoordinator::completePaymentMethodSelection(std::optional<WebCore::ApplePayPaymentMethodUpdate>&& update)
{
    send(Messages::WebPaymentCoordinatorProxy::CompletePaymentMethodSelection(WTFMove(update)));
}

#if ENABLE(APPLE_PAY_COUPON_CODE)

void WebPaymentCoordinator::completeCouponCodeChange(std::optional<WebCore::ApplePayCouponCodeUpdate>&& update)
{
    send(Messages::WebPaymentCoordinatorProxy::CompleteCouponCodeChange(WTFMove(update)));
}

#endif // ENABLE(APPLE_PAY_COUPON_CODE)

void WebPaymentCoordinator::completePaymentSession(WebCore::ApplePayPaymentAuthorizationResult&& result)
{
    send(Messages::WebPaymentCoordinatorProxy::CompletePaymentSession(WTFMove(result)));
}

void WebPaymentCoordinator::abortPaymentSession()
{
    send(Messages::WebPaymentCoordinatorProxy::AbortPaymentSession());
}

void WebPaymentCoordinator::cancelPaymentSession()
{
    send(Messages::WebPaymentCoordinatorProxy::CancelPaymentSession());
}

IPC::Connection* WebPaymentCoordinator::messageSenderConnection() const
{
#if ENABLE(APPLE_PAY_REMOTE_UI)
    return &WebProcess::singleton().ensureNetworkProcessConnection().connection();
#else
    return WebProcess::singleton().parentProcessConnection();
#endif
}

uint64_t WebPaymentCoordinator::messageSenderDestinationID() const
{
    return m_webPage ? m_webPage->identifier().toUInt64() : 0;
}

void WebPaymentCoordinator::validateMerchant(const String& validationURLString)
{
    protectedPaymentCoordinator()->validateMerchant(URL { validationURLString });
}

void WebPaymentCoordinator::didAuthorizePayment(const WebCore::Payment& payment)
{
    protectedPaymentCoordinator()->didAuthorizePayment(payment);
}

void WebPaymentCoordinator::didSelectShippingMethod(const WebCore::ApplePayShippingMethod& shippingMethod)
{
    protectedPaymentCoordinator()->didSelectShippingMethod(shippingMethod);
}

void WebPaymentCoordinator::didSelectShippingContact(const WebCore::PaymentContact& shippingContact)
{
    protectedPaymentCoordinator()->didSelectShippingContact(shippingContact);
}

void WebPaymentCoordinator::didSelectPaymentMethod(const WebCore::PaymentMethod& paymentMethod)
{
    protectedPaymentCoordinator()->didSelectPaymentMethod(paymentMethod);
}

#if ENABLE(APPLE_PAY_COUPON_CODE)

void WebPaymentCoordinator::didChangeCouponCode(String&& couponCode)
{
    protectedPaymentCoordinator()->didChangeCouponCode(WTFMove(couponCode));
}

#endif // ENABLE(APPLE_PAY_COUPON_CODE)

void WebPaymentCoordinator::didCancelPaymentSession(WebCore::PaymentSessionError&& sessionError)
{
    protectedPaymentCoordinator()->didCancelPaymentSession(WTFMove(sessionError));
}

Ref<WebCore::PaymentCoordinator> WebPaymentCoordinator::protectedPaymentCoordinator()
{
    return m_webPage->corePage()->paymentCoordinator();
}

void WebPaymentCoordinator::getSetupFeatures(const WebCore::ApplePaySetupConfiguration& configuration, const URL& url, CompletionHandler<void(Vector<Ref<WebCore::ApplePaySetupFeature>>&&)>&& completionHandler)
{
    RefPtr webPage = m_webPage.get();
    if (!webPage)
        return completionHandler({ });
    webPage->sendWithAsyncReply(Messages::WebPaymentCoordinatorProxy::GetSetupFeatures(PaymentSetupConfiguration { configuration, url }), WTFMove(completionHandler));
}

void WebPaymentCoordinator::beginApplePaySetup(const WebCore::ApplePaySetupConfiguration& configuration, const URL& url, Vector<Ref<WebCore::ApplePaySetupFeature>>&& features, CompletionHandler<void(bool)>&& completionHandler)
{
    RefPtr webPage = m_webPage.get();
    if (!webPage)
        return completionHandler(false);
    webPage->sendWithAsyncReply(Messages::WebPaymentCoordinatorProxy::BeginApplePaySetup(PaymentSetupConfiguration { configuration, url }, PaymentSetupFeatures { WTFMove(features) }), WTFMove(completionHandler));
}

void WebPaymentCoordinator::endApplePaySetup()
{
    if (RefPtr webPage = m_webPage.get())
        webPage->send(Messages::WebPaymentCoordinatorProxy::EndApplePaySetup());
}

}

#endif
