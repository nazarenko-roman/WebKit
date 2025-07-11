/*
 * Copyright (C) 2013-2018 Apple Inc. All rights reserved.
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

#include "config.h"
#include "DFGJITCode.h"

#if ENABLE(DFG_JIT)

#include "CodeBlock.h"
#include "DFGThunks.h"
#include "FTLForOSREntryJITCode.h"
#include "JumpTable.h"

namespace JSC { namespace DFG {

JITData::JITData(unsigned stubInfoSize, unsigned poolSize, const JITCode& jitCode, ExitVector&& exits)
    : Base(stubInfoSize, poolSize)
    , m_callLinkInfos(jitCode.m_unlinkedCallLinkInfos.size())
    , m_exits(WTFMove(exits))
{
    unsigned numberOfWatchpoints = 0;
    for (unsigned i = 0; i < jitCode.m_linkerIR.size(); ++i) {
        auto entry = jitCode.m_linkerIR.at(i);
        switch (entry.type()) {
        case LinkerIR::Type::HavingABadTimeWatchpointSet:
        case LinkerIR::Type::MasqueradesAsUndefinedWatchpointSet:
        case LinkerIR::Type::ArrayBufferDetachWatchpointSet:
        case LinkerIR::Type::ArrayIteratorProtocolWatchpointSet:
        case LinkerIR::Type::NumberToStringWatchpointSet:
        case LinkerIR::Type::StructureCacheClearedWatchpointSet:
        case LinkerIR::Type::StringToStringWatchpointSet:
        case LinkerIR::Type::StringValueOfWatchpointSet:
        case LinkerIR::Type::StringSymbolReplaceWatchpointSet:
        case LinkerIR::Type::StringSymbolToPrimitiveWatchpointSet:
        case LinkerIR::Type::RegExpPrimordialPropertiesWatchpointSet:
        case LinkerIR::Type::ArraySpeciesWatchpointSet:
        case LinkerIR::Type::ArrayPrototypeChainIsSaneWatchpointSet:
        case LinkerIR::Type::StringPrototypeChainIsSaneWatchpointSet:
        case LinkerIR::Type::ObjectPrototypeChainIsSaneWatchpointSet: {
            ++numberOfWatchpoints;
            break;
        }
        case LinkerIR::Type::Invalid:
        case LinkerIR::Type::CallLinkInfo:
        case LinkerIR::Type::CellPointer:
        case LinkerIR::Type::NonCellPointer:
        case LinkerIR::Type::GlobalObject:
            break;
        }
    }
    m_watchpoints = FixedVector<CodeBlockJettisoningWatchpoint>(numberOfWatchpoints);
}

template<typename WatchpointSet>
static bool attemptToWatch(CodeBlock* codeBlock, WatchpointSet& set, CodeBlockJettisoningWatchpoint& watchpoint)
{
    if (set.hasBeenInvalidated())
        return false;
    {
        ConcurrentJSLocker locker(codeBlock->m_lock);
        watchpoint.initialize(codeBlock);
    }
    set.add(&watchpoint);
    return true;
}

bool JITData::tryInitialize(VM& vm, CodeBlock* codeBlock, const JITCode& jitCode)
{
    m_globalObject = codeBlock->globalObject();
    m_stackOffset = codeBlock->stackPointerOffset() * sizeof(Register);

    for (unsigned index = 0; index < jitCode.m_unlinkedStubInfos.size(); ++index) {
        const UnlinkedStructureStubInfo& unlinkedStubInfo = jitCode.m_unlinkedStubInfos[index];
        stubInfo(index).initializeFromDFGUnlinkedStructureStubInfo(codeBlock, unlinkedStubInfo);
    }

    unsigned indexOfWatchpoints = 0;
    bool success = true;
    for (unsigned i = 0; i < jitCode.m_linkerIR.size(); ++i) {
        auto entry = jitCode.m_linkerIR.at(i);
        switch (entry.type()) {
        case LinkerIR::Type::Invalid: {
            trailingSpan()[i] = entry.pointer();
            break;
        }
        case LinkerIR::Type::CallLinkInfo: {
            unsigned index = std::bit_cast<uintptr_t>(entry.pointer());
            const UnlinkedCallLinkInfo& unlinkedCallLinkInfo = jitCode.m_unlinkedCallLinkInfos[index];
            OptimizingCallLinkInfo& callLinkInfo = m_callLinkInfos[index];
            callLinkInfo.initializeFromDFGUnlinkedCallLinkInfo(vm, unlinkedCallLinkInfo, codeBlock);
            trailingSpan()[i] = &callLinkInfo;
            break;
        }
        case LinkerIR::Type::CellPointer: {
            trailingSpan()[i] = entry.pointer();
            break;
        }
        case LinkerIR::Type::NonCellPointer: {
            trailingSpan()[i] = entry.pointer();
            break;
        }
        case LinkerIR::Type::GlobalObject: {
            trailingSpan()[i] = codeBlock->globalObject();
            break;
        }
        case LinkerIR::Type::HavingABadTimeWatchpointSet: {
            auto& watchpoint = m_watchpoints[indexOfWatchpoints++];
            success &= attemptToWatch(codeBlock, m_globalObject->havingABadTimeWatchpointSet(), watchpoint);
            break;
        }
        case LinkerIR::Type::MasqueradesAsUndefinedWatchpointSet: {
            auto& watchpoint = m_watchpoints[indexOfWatchpoints++];
            success &= attemptToWatch(codeBlock, m_globalObject->masqueradesAsUndefinedWatchpointSet(), watchpoint);
            break;
        }
        case LinkerIR::Type::ArrayBufferDetachWatchpointSet: {
            auto& watchpoint = m_watchpoints[indexOfWatchpoints++];
            success &= attemptToWatch(codeBlock, m_globalObject->arrayBufferDetachWatchpointSet(), watchpoint);
            break;
        }
        case LinkerIR::Type::ArrayIteratorProtocolWatchpointSet: {
            auto& watchpoint = m_watchpoints[indexOfWatchpoints++];
            success &= attemptToWatch(codeBlock, m_globalObject->arrayIteratorProtocolWatchpointSet(), watchpoint);
            break;
        }
        case LinkerIR::Type::NumberToStringWatchpointSet: {
            auto& watchpoint = m_watchpoints[indexOfWatchpoints++];
            success &= attemptToWatch(codeBlock, m_globalObject->numberToStringWatchpointSet(), watchpoint);
            break;
        }
        case LinkerIR::Type::StructureCacheClearedWatchpointSet: {
            auto& watchpoint = m_watchpoints[indexOfWatchpoints++];
            success &= attemptToWatch(codeBlock, m_globalObject->structureCacheClearedWatchpointSet(), watchpoint);
            break;
        }
        case LinkerIR::Type::StringToStringWatchpointSet: {
            auto& watchpoint = m_watchpoints[indexOfWatchpoints++];
            success &= attemptToWatch(codeBlock, m_globalObject->stringToStringWatchpointSet(), watchpoint);
            break;
        }
        case LinkerIR::Type::StringValueOfWatchpointSet: {
            auto& watchpoint = m_watchpoints[indexOfWatchpoints++];
            success &= attemptToWatch(codeBlock, m_globalObject->stringValueOfWatchpointSet(), watchpoint);
            break;
        }
        case LinkerIR::Type::StringSymbolReplaceWatchpointSet: {
            auto& watchpoint = m_watchpoints[indexOfWatchpoints++];
            success &= attemptToWatch(codeBlock, m_globalObject->stringSymbolReplaceWatchpointSet(), watchpoint);
            break;
        }
        case LinkerIR::Type::StringSymbolToPrimitiveWatchpointSet: {
            auto& watchpoint = m_watchpoints[indexOfWatchpoints++];
            success &= attemptToWatch(codeBlock, m_globalObject->stringSymbolToPrimitiveWatchpointSet(), watchpoint);
            break;
        }
        case LinkerIR::Type::RegExpPrimordialPropertiesWatchpointSet: {
            auto& watchpoint = m_watchpoints[indexOfWatchpoints++];
            success &= attemptToWatch(codeBlock, m_globalObject->regExpPrimordialPropertiesWatchpointSet(), watchpoint);
            break;
        }
        case LinkerIR::Type::ArraySpeciesWatchpointSet: {
            auto& watchpoint = m_watchpoints[indexOfWatchpoints++];
            success &= attemptToWatch(codeBlock, m_globalObject->arraySpeciesWatchpointSet(), watchpoint);
            break;
        }
        case LinkerIR::Type::ArrayPrototypeChainIsSaneWatchpointSet: {
            auto& watchpoint = m_watchpoints[indexOfWatchpoints++];
            success &= attemptToWatch(codeBlock, m_globalObject->arrayPrototypeChainIsSaneWatchpointSet(), watchpoint);
            break;
        }
        case LinkerIR::Type::StringPrototypeChainIsSaneWatchpointSet: {
            auto& watchpoint = m_watchpoints[indexOfWatchpoints++];
            success &= attemptToWatch(codeBlock, m_globalObject->stringPrototypeChainIsSaneWatchpointSet(), watchpoint);
            break;
        }
        case LinkerIR::Type::ObjectPrototypeChainIsSaneWatchpointSet: {
            auto& watchpoint = m_watchpoints[indexOfWatchpoints++];
            success &= attemptToWatch(codeBlock, m_globalObject->objectPrototypeChainIsSaneWatchpointSet(), watchpoint);
            break;
        }
        }
    }
    return success;
}

JITCode::JITCode(bool isUnlinked)
    : DirectJITCode(JITType::DFGJIT)
    , common(isUnlinked)
{
}

JITCode::~JITCode() = default;

CommonData* JITCode::dfgCommon()
{
    return &common;
}

const CommonData* JITCode::dfgCommon() const
{
    return &common;
}

JITCode* JITCode::dfg()
{
    return this;
}

void JITCode::shrinkToFit()
{
    common.shrinkToFit();
    minifiedDFG.prepareAndShrink();
}

void JITCode::reconstruct(
    CodeBlock* codeBlock, CodeOrigin codeOrigin, unsigned streamIndex,
    Operands<ValueRecovery>& result)
{
    variableEventStream.reconstruct(codeBlock, codeOrigin, minifiedDFG, streamIndex, result);
}

void JITCode::reconstruct(CallFrame* callFrame, CodeBlock* codeBlock, CodeOrigin codeOrigin, unsigned streamIndex, Operands<std::optional<JSValue>>& result)
{
    Operands<ValueRecovery> recoveries;
    reconstruct(codeBlock, codeOrigin, streamIndex, recoveries);
    
    result = Operands<std::optional<JSValue>>(OperandsLike, recoveries);
    for (size_t i = result.size(); i--;)
        result[i] = recoveries[i].recover(callFrame);
}

RegisterSetBuilder JITCode::liveRegistersToPreserveAtExceptionHandlingCallSite(CodeBlock* codeBlock, CallSiteIndex callSiteIndex)
{
    for (OSRExit& exit : m_osrExit) {
        if (exit.isExceptionHandler() && exit.m_exceptionHandlerCallSiteIndex.bits() == callSiteIndex.bits()) {
            Operands<ValueRecovery> valueRecoveries;
            reconstruct(codeBlock, exit.m_codeOrigin, exit.m_streamIndex, valueRecoveries);
            RegisterSetBuilder liveAtOSRExit;
            for (size_t index = 0; index < valueRecoveries.size(); ++index) {
                const ValueRecovery& recovery = valueRecoveries[index];
                if (recovery.isInRegisters()) {
                    if (recovery.isInGPR())
                        liveAtOSRExit.add(recovery.gpr(), IgnoreVectors);
                    else if (recovery.isInFPR())
                        liveAtOSRExit.add(recovery.fpr(), IgnoreVectors);
#if USE(JSVALUE32_64)
                    else if (recovery.isInJSValueRegs()) {
                        liveAtOSRExit.add(recovery.payloadGPR(), IgnoreVectors);
                        liveAtOSRExit.add(recovery.tagGPR(), IgnoreVectors);
                    }
#endif
                    else
                        RELEASE_ASSERT_NOT_REACHED();
                }
            }

            return liveAtOSRExit;
        }
    }

    return { };
}

#if ENABLE(FTL_JIT)
bool JITCode::checkIfOptimizationThresholdReached(CodeBlock* codeBlock)
{
    ASSERT(codeBlock->jitType() == JITType::DFGJIT);
    return codeBlock->dfgJITData()->tierUpCounter().checkIfThresholdCrossedAndSet(codeBlock);
}

void JITCode::optimizeNextInvocation(CodeBlock* codeBlock)
{
    ASSERT(codeBlock->jitType() == JITType::DFGJIT);
    dataLogLnIf(Options::verboseOSR(), *codeBlock, ": FTL-optimizing next invocation.");
    codeBlock->dfgJITData()->tierUpCounter().setNewThreshold(0, codeBlock);
}

void JITCode::dontOptimizeAnytimeSoon(CodeBlock* codeBlock)
{
    ASSERT(codeBlock->jitType() == JITType::DFGJIT);
    dataLogLnIf(Options::verboseOSR(), *codeBlock, ": Not FTL-optimizing anytime soon.");
    codeBlock->dfgJITData()->tierUpCounter().deferIndefinitely();
}

void JITCode::optimizeAfterWarmUp(CodeBlock* codeBlock)
{
    ASSERT(codeBlock->jitType() == JITType::DFGJIT);
    dataLogLnIf(Options::verboseOSR(), *codeBlock, ": FTL-optimizing after warm-up.");
    CodeBlock* baseline = codeBlock->baselineVersion();
    codeBlock->dfgJITData()->tierUpCounter().setNewThreshold(baseline->adjustedCounterValue(Options::thresholdForFTLOptimizeAfterWarmUp()), baseline);
}

void JITCode::optimizeSoon(CodeBlock* codeBlock)
{
    ASSERT(codeBlock->jitType() == JITType::DFGJIT);
    dataLogLnIf(Options::verboseOSR(), *codeBlock, ": FTL-optimizing soon.");
    CodeBlock* baseline = codeBlock->baselineVersion();
    codeBlock->dfgJITData()->tierUpCounter().setNewThreshold(baseline->adjustedCounterValue(Options::thresholdForFTLOptimizeSoon()), codeBlock);
}

void JITCode::forceOptimizationSlowPathConcurrently(CodeBlock* codeBlock)
{
    ASSERT(codeBlock->jitType() == JITType::DFGJIT);
    dataLogLnIf(Options::verboseOSR(), *codeBlock, ": Forcing slow path concurrently for FTL entry.");
    codeBlock->dfgJITData()->tierUpCounter().forceSlowPathConcurrently();
}

void JITCode::setOptimizationThresholdBasedOnCompilationResult(
    CodeBlock* codeBlock, CompilationResult result)
{
    ASSERT(codeBlock->jitType() == JITType::DFGJIT);
    switch (result) {
    case CompilationResult::CompilationSuccessful:
        optimizeNextInvocation(codeBlock);
        codeBlock->baselineVersion()->m_hasBeenCompiledWithFTL = true;
        return;
    case CompilationResult::CompilationFailed:
        dontOptimizeAnytimeSoon(codeBlock);
        codeBlock->baselineVersion()->m_didFailFTLCompilation = true;
        return;
    case CompilationResult::CompilationDeferred:
        optimizeAfterWarmUp(codeBlock);
        return;
    case CompilationResult::CompilationInvalidated:
        // This is weird - it will only happen in cases when the DFG code block (i.e.
        // the code block that this JITCode belongs to) is also invalidated. So it
        // doesn't really matter what we do. But, we do the right thing anyway. Note
        // that us counting the reoptimization actually means that we might count it
        // twice. But that's generally OK. It's better to overcount reoptimizations
        // than it is to undercount them.
        codeBlock->baselineVersion()->countReoptimization();
        optimizeAfterWarmUp(codeBlock);
        return;
    }
    RELEASE_ASSERT_NOT_REACHED();
}

void JITCode::setOSREntryBlock(VM& vm, const JSCell* owner, CodeBlock* osrEntryBlock)
{
    if (Options::verboseOSR()) {
        dataLogLn(RawPointer(this), ": Setting OSR entry block to ", RawPointer(osrEntryBlock));
        dataLogLn("OSR entries will go to ", osrEntryBlock->jitCode()->ftlForOSREntry()->addressForCall(ArityCheckMode::ArityCheckNotRequired));
    }
    m_osrEntryBlock.set(vm, owner, osrEntryBlock);
}

void JITCode::clearOSREntryBlockAndResetThresholds(CodeBlock *dfgCodeBlock)
{ 
    ASSERT(m_osrEntryBlock);

    BytecodeIndex osrEntryBytecode = m_osrEntryBlock->jitCode()->ftlForOSREntry()->bytecodeIndex();
    m_osrEntryBlock.clear();
    osrEntryRetry = 0;
    tierUpEntryTriggers.set(osrEntryBytecode, JITCode::TriggerReason::DontTrigger);
    setOptimizationThresholdBasedOnCompilationResult(dfgCodeBlock, CompilationResult::CompilationDeferred);
}
#endif // ENABLE(FTL_JIT)

void JITCode::validateReferences(const TrackedReferences& trackedReferences)
{
    common.validateReferences(trackedReferences);
    
    for (OSREntryData& entry : m_osrEntry) {
        for (unsigned i = entry.m_expectedValues.size(); i--;)
            entry.m_expectedValues[i].validateReferences(trackedReferences);
    }
    
    minifiedDFG.validateReferences(trackedReferences);
}

std::optional<CodeOrigin> JITCode::findPC(CodeBlock* codeBlock, void* pc)
{
    const auto* jitData = codeBlock->dfgJITData();
    auto osrExitThunk = codeBlock->vm().getCTIStub(osrExitGenerationThunkGenerator).retagged<OSRExitPtrTag>();
    for (unsigned exitIndex = 0; exitIndex < m_osrExit.size(); ++exitIndex) {
        const auto& codeRef = jitData->exitCode(exitIndex);
        if (ExecutableMemoryHandle* handle = codeRef.executableMemory()) {
            if (handle != osrExitThunk.executableMemory()) {
                if (handle->start().untaggedPtr() <= pc && pc < handle->end().untaggedPtr()) {
                    OSRExit& exit = m_osrExit[exitIndex];
                    return std::optional<CodeOrigin>(exit.m_codeOriginForExitProfile);
                }
            }
        }
    }

    return std::nullopt;
}

void JITCode::finalizeOSREntrypoints(Vector<OSREntryData>&& osrEntry)
{
    auto comparator = [] (const auto& a, const auto& b) {
        return a.m_bytecodeIndex < b.m_bytecodeIndex;
    };
    std::sort(osrEntry.begin(), osrEntry.end(), comparator);

#if ASSERT_ENABLED
    auto verifyIsSorted = [&] (auto& osrVector) {
        for (unsigned i = 0; i + 1 < osrVector.size(); ++i)
            ASSERT(osrVector[i].m_bytecodeIndex <= osrVector[i + 1].m_bytecodeIndex);
    };
    verifyIsSorted(osrEntry);
#endif
    m_osrEntry = WTFMove(osrEntry);
}

} } // namespace JSC::DFG

#endif // ENABLE(DFG_JIT)
