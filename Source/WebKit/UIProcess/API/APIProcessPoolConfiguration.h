/*
 * Copyright (C) 2014-2022 Apple Inc. All rights reserved.
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

#include "APIObject.h"
#include "CacheModel.h"
#include "WebsiteDataStore.h"
#include <wtf/MemoryPressureHandler.h>
#include <wtf/ProcessID.h>
#include <wtf/Ref.h>
#include <wtf/Vector.h>
#include <wtf/text/CString.h>
#include <wtf/text/WTFString.h>

namespace API {

#if PLATFORM(COCOA) && !PLATFORM(IOS_FAMILY_SIMULATOR)
#define DEFAULT_CAPTURE_DISPLAY_IN_UI_PROCESS true
#else
#define DEFAULT_CAPTURE_DISPLAY_IN_UI_PROCESS false
#endif

class ProcessPoolConfiguration final : public ObjectImpl<Object::Type::ProcessPoolConfiguration> {
public:
    static Ref<ProcessPoolConfiguration> create();

    explicit ProcessPoolConfiguration();
    virtual ~ProcessPoolConfiguration();
    
    Ref<ProcessPoolConfiguration> copy();

    bool usesSingleWebProcess() const { return m_usesSingleWebProcess; }
    void setUsesSingleWebProcess(bool enabled) { m_usesSingleWebProcess = enabled; }

    bool isAutomaticProcessWarmingEnabled() const
    {
        return m_isAutomaticProcessWarmingEnabledByClient.value_or(m_clientWouldBenefitFromAutomaticProcessPrewarming);
    }

    bool wasAutomaticProcessWarmingSetByClient() const { return !!m_isAutomaticProcessWarmingEnabledByClient; }
    void setIsAutomaticProcessWarmingEnabled(bool value) { m_isAutomaticProcessWarmingEnabledByClient = value; }

    void setUsesWebProcessCache(bool value) { m_usesWebProcessCache = value; }
    bool usesWebProcessCache() const { return m_usesWebProcessCache; }

    bool clientWouldBenefitFromAutomaticProcessPrewarming() const { return m_clientWouldBenefitFromAutomaticProcessPrewarming; }
    void setClientWouldBenefitFromAutomaticProcessPrewarming(bool value) { m_clientWouldBenefitFromAutomaticProcessPrewarming = value; }

    void setUsesBackForwardCache(bool value) { m_usesBackForwardCache = value; }
    bool usesBackForwardCache() const { return m_usesBackForwardCache; }

    const WTF::String& injectedBundlePath() const { return m_injectedBundlePath; }
    void setInjectedBundlePath(const WTF::String& injectedBundlePath) { m_injectedBundlePath = injectedBundlePath; }

    const Vector<WTF::String>& cachePartitionedURLSchemes() { return m_cachePartitionedURLSchemes; }
    void setCachePartitionedURLSchemes(Vector<WTF::String>&& cachePartitionedURLSchemes) { m_cachePartitionedURLSchemes = WTFMove(cachePartitionedURLSchemes); }

    const Vector<WTF::String>& alwaysRevalidatedURLSchemes() { return m_alwaysRevalidatedURLSchemes; }
    void setAlwaysRevalidatedURLSchemes(Vector<WTF::String>&& alwaysRevalidatedURLSchemes) { m_alwaysRevalidatedURLSchemes = WTFMove(alwaysRevalidatedURLSchemes); }

    const Vector<WTF::String>& additionalReadAccessAllowedPaths() { return m_additionalReadAccessAllowedPaths; }
    void setAdditionalReadAccessAllowedPaths(Vector<WTF::String>&& additionalReadAccessAllowedPaths) { m_additionalReadAccessAllowedPaths = additionalReadAccessAllowedPaths; }

    bool fullySynchronousModeIsAllowedForTesting() const { return m_fullySynchronousModeIsAllowedForTesting; }
    void setFullySynchronousModeIsAllowedForTesting(bool allowed) { m_fullySynchronousModeIsAllowedForTesting = allowed; }

    bool ignoreSynchronousMessagingTimeoutsForTesting() const { return m_ignoreSynchronousMessagingTimeoutsForTesting; }
    void setIgnoreSynchronousMessagingTimeoutsForTesting(bool allowed) { m_ignoreSynchronousMessagingTimeoutsForTesting = allowed; }

    bool attrStyleEnabled() const { return m_attrStyleEnabled; }
    void setAttrStyleEnabled(bool enabled) { m_attrStyleEnabled = enabled; }
    
    bool shouldThrowExceptionForGlobalConstantRedeclaration() const { return m_shouldThrowExceptionForGlobalConstantRedeclaration; }
    void setShouldThrowExceptionForGlobalConstantRedeclaration(bool shouldThrow) { m_shouldThrowExceptionForGlobalConstantRedeclaration = shouldThrow; }
    
    bool alwaysRunsAtBackgroundPriority() const { return m_alwaysRunsAtBackgroundPriority; }
    void setAlwaysRunsAtBackgroundPriority(bool alwaysRunsAtBackgroundPriority) { m_alwaysRunsAtBackgroundPriority = alwaysRunsAtBackgroundPriority; }

    bool shouldTakeUIBackgroundAssertion() const { return m_shouldTakeUIBackgroundAssertion; }
    void setShouldTakeUIBackgroundAssertion(bool shouldTakeUIBackgroundAssertion) { m_shouldTakeUIBackgroundAssertion = shouldTakeUIBackgroundAssertion; }

    bool shouldCaptureDisplayInUIProcess() const { return m_shouldCaptureDisplayInUIProcess; }
    void setShouldCaptureDisplayInUIProcess(bool shouldCaptureDisplayInUIProcess) { m_shouldCaptureDisplayInUIProcess = shouldCaptureDisplayInUIProcess; }

    bool shouldConfigureJSCForTesting() const { return m_shouldConfigureJSCForTesting; }
    void setShouldConfigureJSCForTesting(bool value) { m_shouldConfigureJSCForTesting = value; }
    bool isJITEnabled() const { return m_isJITEnabled; }
    void setJITEnabled(bool enabled) { m_isJITEnabled = enabled; }

    ProcessID presentingApplicationPID() const { return m_presentingApplicationPID; }
    void setPresentingApplicationPID(ProcessID pid) { m_presentingApplicationPID = pid; }

#if HAVE(AUDIT_TOKEN)
    const std::optional<audit_token_t> presentingApplicationProcessToken() const { return m_presentingApplicationProcessToken; }
    void setPresentingApplicationProcessToken(std::optional<audit_token_t>&& token) { m_presentingApplicationProcessToken = WTFMove(token); }
#endif

    bool processSwapsOnNavigation() const
    {
        return m_processSwapsOnNavigationFromClient.value_or(m_processSwapsOnNavigationFromExperimentalFeatures);
    }
    void setProcessSwapsOnNavigation(bool swaps) { m_processSwapsOnNavigationFromClient = swaps; }
    void setProcessSwapsOnNavigationFromExperimentalFeatures(bool swaps) { m_processSwapsOnNavigationFromExperimentalFeatures = swaps; }

    bool alwaysKeepAndReuseSwappedProcesses() const { return m_alwaysKeepAndReuseSwappedProcesses; }
    void setAlwaysKeepAndReuseSwappedProcesses(bool keepAndReuse) { m_alwaysKeepAndReuseSwappedProcesses = keepAndReuse; }

    bool processSwapsOnNavigationWithinSameNonHTTPFamilyProtocol() const { return m_processSwapsOnNavigationWithinSameNonHTTPFamilyProtocol; }
    void setProcessSwapsOnNavigationWithinSameNonHTTPFamilyProtocol(bool swaps) { m_processSwapsOnNavigationWithinSameNonHTTPFamilyProtocol = swaps; }

#if PLATFORM(GTK) && !USE(GTK4) && USE(CAIRO)
    bool useSystemAppearanceForScrollbars() const { return m_useSystemAppearanceForScrollbars; }
    void setUseSystemAppearanceForScrollbars(bool useSystemAppearanceForScrollbars) { m_useSystemAppearanceForScrollbars = useSystemAppearanceForScrollbars; }
#endif

#if PLATFORM(PLAYSTATION)
    const WTF::String& webProcessPath() const { return m_webProcessPath; }
    void setWebProcessPath(const WTF::String& webProcessPath) { m_webProcessPath = webProcessPath; }

    const WTF::String& networkProcessPath() const { return m_networkProcessPath; }
    void setNetworkProcessPath(const WTF::String& networkProcessPath) { m_networkProcessPath = networkProcessPath; }

    int32_t userId() const { return m_userId; }
    void setUserId(const int32_t userId) { m_userId = userId; }
#endif

#if PLATFORM(GTK) || PLATFORM(WPE)
    void setMemoryPressureHandlerConfiguration(const MemoryPressureHandler::Configuration& configuration) { m_memoryPressureHandlerConfiguration = configuration; }
    const std::optional<MemoryPressureHandler::Configuration>& memoryPressureHandlerConfiguration() const { return m_memoryPressureHandlerConfiguration; }

    bool disableFontHintingForTesting() const { return m_disableFontHintingForTesting; }
    void setDisableFontHintingForTesting(bool override) { m_disableFontHintingForTesting = override; }
#endif

    void setTimeZoneOverride(const WTF::String& timeZoneOverride) { m_timeZoneOverride = timeZoneOverride; }
    const WTF::String& timeZoneOverride() const { return m_timeZoneOverride; }

    void setMemoryFootprintPollIntervalForTesting(Seconds interval) { m_memoryFootprintPollIntervalForTesting = interval; }
    Seconds memoryFootprintPollIntervalForTesting() const { return m_memoryFootprintPollIntervalForTesting; }

    void setMemoryFootprintNotificationThresholds(Vector<uint64_t>&& thresholds) { m_memoryFootprintNotificationThresholds = WTFMove(thresholds); }
    const Vector<uint64_t>& memoryFootprintNotificationThresholds() const { return m_memoryFootprintNotificationThresholds; }

#if ENABLE(WEB_PROCESS_SUSPENSION_DELAY)
    void setSuspendsWebProcessesAggressivelyOnMemoryPressure(bool enabled) { m_suspendsWebProcessesAggressivelyOnMemoryPressure = enabled; }
    bool suspendsWebProcessesAggressivelyOnMemoryPressure() { return m_suspendsWebProcessesAggressivelyOnMemoryPressure; }
#endif

private:
    WTF::String m_injectedBundlePath;
    Vector<WTF::String> m_cachePartitionedURLSchemes;
    Vector<WTF::String> m_alwaysRevalidatedURLSchemes;
    Vector<WTF::String> m_additionalReadAccessAllowedPaths;
    bool m_fullySynchronousModeIsAllowedForTesting { false };
    bool m_ignoreSynchronousMessagingTimeoutsForTesting { false };
    bool m_attrStyleEnabled { false };
    bool m_shouldThrowExceptionForGlobalConstantRedeclaration { true };
    bool m_alwaysRunsAtBackgroundPriority { false };
    bool m_shouldTakeUIBackgroundAssertion { true };
    bool m_shouldCaptureDisplayInUIProcess { DEFAULT_CAPTURE_DISPLAY_IN_UI_PROCESS };
    ProcessID m_presentingApplicationPID { getCurrentProcessID() };
    std::optional<bool> m_processSwapsOnNavigationFromClient;
    bool m_processSwapsOnNavigationFromExperimentalFeatures { false };
    bool m_alwaysKeepAndReuseSwappedProcesses { false };
    bool m_processSwapsOnNavigationWithinSameNonHTTPFamilyProtocol { false };
    std::optional<bool> m_isAutomaticProcessWarmingEnabledByClient;
    bool m_usesWebProcessCache { false };
    bool m_usesBackForwardCache { true };
    bool m_clientWouldBenefitFromAutomaticProcessPrewarming { false };
    bool m_shouldConfigureJSCForTesting { false };
    bool m_isJITEnabled { true };
    bool m_usesSingleWebProcess { false };
#if PLATFORM(GTK) && !USE(GTK4) && USE(CAIRO)
    bool m_useSystemAppearanceForScrollbars { false };
#endif
#if PLATFORM(PLAYSTATION)
    WTF::String m_webProcessPath;
    WTF::String m_networkProcessPath;
    int32_t m_userId { -1 };
#endif
#if PLATFORM(GTK) || PLATFORM(WPE)
    std::optional<MemoryPressureHandler::Configuration> m_memoryPressureHandlerConfiguration;
    bool m_disableFontHintingForTesting { false };
#endif
#if HAVE(AUDIT_TOKEN)
    std::optional<audit_token_t> m_presentingApplicationProcessToken;
#endif
    WTF::String m_timeZoneOverride;
    Seconds m_memoryFootprintPollIntervalForTesting;
    Vector<uint64_t> m_memoryFootprintNotificationThresholds;
#if ENABLE(WEB_PROCESS_SUSPENSION_DELAY)
    bool m_suspendsWebProcessesAggressivelyOnMemoryPressure { false };
#endif
};

} // namespace API

SPECIALIZE_TYPE_TRAITS_API_OBJECT(ProcessPoolConfiguration);
