/*
 * Copyright (C) 2015-2017 Apple Inc. All rights reserved.
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
#include "IDBRequest.h"

#include "DOMException.h"
#include "Event.h"
#include "EventDispatcher.h"
#include "EventNames.h"
#include "EventTargetInterfaces.h"
#include "IDBBindingUtilities.h"
#include "IDBConnectionProxy.h"
#include "IDBCursor.h"
#include "IDBDatabase.h"
#include "IDBIndex.h"
#include "IDBKeyData.h"
#include "IDBObjectStore.h"
#include "IDBResultData.h"
#include "JSDOMConvertIndexedDB.h"
#include "JSDOMConvertNumbers.h"
#include "JSDOMConvertSequences.h"
#include "Logging.h"
#include "ScriptExecutionContext.h"
#include "ThreadSafeDataBuffer.h"
#include "WebCoreOpaqueRoot.h"
#include <JavaScriptCore/StrongInlines.h>
#include <wtf/Scope.h>
#include <wtf/TZoneMallocInlines.h>

namespace WebCore {
using namespace JSC;

WTF_MAKE_TZONE_OR_ISO_ALLOCATED_IMPL(IDBRequest);

Ref<IDBRequest> IDBRequest::create(ScriptExecutionContext& context, IDBObjectStore& objectStore, IDBTransaction& transaction)
{
    auto request = adoptRef(*new IDBRequest(context, objectStore, transaction));
    request->suspendIfNeeded();
    return request;
}

Ref<IDBRequest> IDBRequest::create(ScriptExecutionContext& context, IDBCursor& cursor, IDBTransaction& transaction)
{
    auto request = adoptRef(*new IDBRequest(context, cursor, transaction));
    request->suspendIfNeeded();
    return request;
}

Ref<IDBRequest> IDBRequest::create(ScriptExecutionContext& context, IDBIndex& index, IDBTransaction& transaction)
{
    auto request = adoptRef(*new IDBRequest(context, index, transaction));
    request->suspendIfNeeded();
    return request;
}

Ref<IDBRequest> IDBRequest::createObjectStoreGet(ScriptExecutionContext& context, IDBObjectStore& objectStore, IndexedDB::ObjectStoreRecordType type, IDBTransaction& transaction)
{
    auto request = adoptRef(*new IDBRequest(context, objectStore, type, transaction));
    request->suspendIfNeeded();
    return request;
}

Ref<IDBRequest> IDBRequest::createIndexGet(ScriptExecutionContext& context, IDBIndex& index, IndexedDB::IndexRecordType requestedRecordType, IDBTransaction& transaction)
{
    auto request = adoptRef(*new IDBRequest(context, index, requestedRecordType, transaction));
    request->suspendIfNeeded();
    return request;
}

IDBRequest::IDBRequest(ScriptExecutionContext& context, IDBClient::IDBConnectionProxy& connectionProxy, IndexedDB::RequestType requestType)
    : IDBActiveDOMObject(&context)
    , m_resourceIdentifier(connectionProxy)
    , m_result(NullResultType::Undefined)
    , m_connectionProxy(connectionProxy)
    , m_requestType(requestType)
{
}

IDBRequest::IDBRequest(ScriptExecutionContext& context, IDBObjectStore& objectStore, IDBTransaction& transaction)
    : IDBActiveDOMObject(&context)
    , m_transaction(&transaction)
    , m_resourceIdentifier(transaction.connectionProxy())
    , m_result(NullResultType::Undefined)
    , m_source(&objectStore)
    , m_connectionProxy(transaction.database().connectionProxy())
{
}

IDBRequest::IDBRequest(ScriptExecutionContext& context, IDBCursor& cursor, IDBTransaction& transaction)
    : IDBActiveDOMObject(&context)
    , m_transaction(&transaction)
    , m_resourceIdentifier(transaction.connectionProxy())
    , m_result(NullResultType::Undefined)
    , m_pendingCursor(&cursor)
    , m_connectionProxy(transaction.database().connectionProxy())
{
    WTF::switchOn(cursor.source(),
        [this] (const auto& value) { this->m_source = IDBRequest::Source { value }; }
    );

    cursor.setRequest(*this);
}

IDBRequest::IDBRequest(ScriptExecutionContext& context, IDBIndex& index, IDBTransaction& transaction)
    : IDBActiveDOMObject(&context)
    , m_transaction(&transaction)
    , m_resourceIdentifier(transaction.connectionProxy())
    , m_result(NullResultType::Undefined)
    , m_source(&index)
    , m_connectionProxy(transaction.database().connectionProxy())
{
}

IDBRequest::IDBRequest(ScriptExecutionContext& context, IDBObjectStore& objectStore, IndexedDB::ObjectStoreRecordType type, IDBTransaction& transaction)
    : IDBActiveDOMObject(&context)
    , m_transaction(&transaction)
    , m_resourceIdentifier(transaction.connectionProxy())
    , m_result(NullResultType::Undefined)
    , m_source(&objectStore)
    , m_connectionProxy(transaction.database().connectionProxy())
    , m_requestedObjectStoreRecordType(type)
{
}

IDBRequest::IDBRequest(ScriptExecutionContext& context, IDBIndex& index, IndexedDB::IndexRecordType requestedRecordType, IDBTransaction& transaction)
    : IDBRequest(context, index, transaction)
{
    m_requestedIndexRecordType = requestedRecordType;
}

IDBRequest::~IDBRequest()
{
    ASSERT(canCurrentThreadAccessThreadLocalData(originThread()));

    WTF::switchOn(m_result,
        [] (RefPtr<IDBCursor>& cursor) { cursor->clearRequest(); },
        [] (const auto&) { }
    );
}

ExceptionOr<IDBRequest::Result> IDBRequest::result() const
{
    if (!isDone())
        return Exception { ExceptionCode::InvalidStateError, "Failed to read the 'result' property from 'IDBRequest': The request has not finished."_s };

    return IDBRequest::Result { m_result };
}

ExceptionOr<DOMException*> IDBRequest::error() const
{
    ASSERT(canCurrentThreadAccessThreadLocalData(originThread()));

    if (!isDone())
        return Exception { ExceptionCode::InvalidStateError, "Failed to read the 'error' property from 'IDBRequest': The request has not finished."_s };

    return m_domError.get();
}

void IDBRequest::setSource(IDBCursor& cursor)
{
    ASSERT(canCurrentThreadAccessThreadLocalData(originThread()));

    m_source = Source { &cursor };
}

void IDBRequest::setVersionChangeTransaction(IDBTransaction& transaction)
{
    ASSERT(canCurrentThreadAccessThreadLocalData(originThread()));
    ASSERT(!m_transaction);
    ASSERT(transaction.isVersionChange());
    ASSERT(!transaction.isFinishedOrFinishing());

    m_transaction = transaction;
}

RefPtr<WebCore::IDBTransaction> IDBRequest::transaction() const
{
    ASSERT(canCurrentThreadAccessThreadLocalData(originThread()));
    return m_shouldExposeTransactionToDOM ? m_transaction : nullptr;
}

std::optional<IDBObjectStoreIdentifier> IDBRequest::sourceObjectStoreIdentifier() const
{
    ASSERT(canCurrentThreadAccessThreadLocalData(originThread()));

    if (!m_source)
        return std::nullopt;

    return WTF::switchOn(m_source.value(),
        [] (const RefPtr<IDBObjectStore>& objectStore) -> std::optional<IDBObjectStoreIdentifier> { return objectStore->info().identifier(); },
        [] (const RefPtr<IDBIndex>& index) -> std::optional<IDBObjectStoreIdentifier> { return index->info().objectStoreIdentifier(); },
        [] (const RefPtr<IDBCursor>&) -> std::optional<IDBObjectStoreIdentifier> { return std::nullopt; }
    );
}

std::optional<IDBIndexIdentifier> IDBRequest::sourceIndexIdentifier() const
{
    ASSERT(canCurrentThreadAccessThreadLocalData(originThread()));

    if (!m_source)
        return std::nullopt;

    if (auto* index = std::get_if<RefPtr<IDBIndex>>(&m_source.value()))
        return (*index)->info().identifier();
    return std::nullopt;
}

IndexedDB::ObjectStoreRecordType IDBRequest::requestedObjectStoreRecordType() const
{
    ASSERT(canCurrentThreadAccessThreadLocalData(originThread()));

    return m_requestedObjectStoreRecordType;
}

IndexedDB::IndexRecordType IDBRequest::requestedIndexRecordType() const
{
    ASSERT(canCurrentThreadAccessThreadLocalData(originThread()));
    ASSERT(m_source);
    ASSERT(std::holds_alternative<RefPtr<IDBIndex>>(m_source.value()));

    return m_requestedIndexRecordType;
}

enum EventTargetInterfaceType IDBRequest::eventTargetInterface() const
{
    ASSERT(canCurrentThreadAccessThreadLocalData(originThread()));

    return EventTargetInterfaceType::IDBRequest;
}

bool IDBRequest::virtualHasPendingActivity() const
{
    ASSERT(canCurrentThreadAccessThreadLocalData(originThread()) || Thread::mayBeGCThread());
    return m_pendingActivity != PendingActivityType::None;
}

void IDBRequest::stop()
{
    ASSERT(canCurrentThreadAccessThreadLocalData(originThread()));

    cancelForStop();

    removeAllEventListeners();

    clearWrappers();
    m_pendingActivity = PendingActivityType::None;
}

void IDBRequest::cancelForStop()
{
    // The base IDBRequest class has nothing additional to do here.
}

void IDBRequest::enqueueEvent(Ref<Event>&& event)
{
    ASSERT(canCurrentThreadAccessThreadLocalData(originThread()));
    if (isContextStopped())
        return;

    queueTaskToDispatchEvent(*this, TaskSource::DatabaseAccess, WTFMove(event));
}

void IDBRequest::dispatchEvent(Event& event)
{
    LOG(IndexedDB, "IDBRequest::dispatchEvent - %s (%p)", event.type().string().utf8().data(), this);

    ASSERT(canCurrentThreadAccessThreadLocalData(originThread()));
    ASSERT(!isContextStopped());

    Ref protectedThis { *this };
    if (!event.isTrusted()) {
        EventDispatcher::dispatchEvent(std::initializer_list<EventTarget*>({ this }), event);
        return;
    }

    m_eventBeingDispatched = event;

    if (event.type() != eventNames().blockedEvent) {
        m_readyState = ReadyState::Done;
        if (m_pendingActivity != PendingActivityType::None && (event.type() == eventNames().successEvent || event.type() == eventNames().errorEvent)) {
            WTF::switchOn(m_result, [&] (const RefPtr<IDBCursor>& cursor) mutable {
                if (!!cursor.get() && m_transaction && !m_transaction->isFinishedOrFinishing())
                    m_pendingActivity = PendingActivityType::CursorIteration;
                else
                    m_pendingActivity = PendingActivityType::None;
                }, [&] (const auto&) mutable {
                    m_pendingActivity = PendingActivityType::None;
                }
            );
        }
    }

    bool shouldDispatchOnTransaction = false;
    if (&event == m_openDatabaseSuccessEvent)
        m_openDatabaseSuccessEvent = nullptr;
    else if (m_transaction && !m_transaction->didDispatchAbortOrCommit())
        shouldDispatchOnTransaction = true;

    {
        TransactionActivator activator(transaction().get());
        if (shouldDispatchOnTransaction)
            EventDispatcher::dispatchEvent(std::initializer_list<EventTarget*>({ this, m_transaction.get(), &m_transaction->database() }), event);
        else
            EventDispatcher::dispatchEvent(std::initializer_list<EventTarget*>({ this }), event);
    }

    m_eventBeingDispatched = nullptr;
    if (!m_transaction)
        return;

    if (m_hasUncaughtException)
        m_transaction->abortDueToFailedRequest(DOMException::create(ExceptionCode::AbortError, "IDBTransaction will abort due to uncaught exception in an event handler"_s));
    else if (!event.defaultPrevented() && event.type() == eventNames().errorEvent && !m_transaction->isFinishedOrFinishing()) {
        ASSERT(m_domError);
        m_transaction->abortDueToFailedRequest(*m_domError);
    }

    m_transaction->finishedDispatchEventForRequest(*this);

    // The request should only remain in the transaction's request list if it represents a pending cursor operation, or this is an open request that was blocked.
    if (!m_pendingCursor && event.type() != eventNames().blockedEvent)
        m_transaction->removeRequest(*this);
}

void IDBRequest::uncaughtExceptionInEventHandler()
{
    LOG(IndexedDB, "IDBRequest::uncaughtExceptionInEventHandler");

    ASSERT(canCurrentThreadAccessThreadLocalData(originThread()));

    if (m_eventBeingDispatched) {
        ASSERT(!m_hasUncaughtException);
        m_hasUncaughtException = true;
        return;
    }
    if (m_transaction && m_idbError.code() != ExceptionCode::AbortError)
        m_transaction->abortDueToFailedRequest(DOMException::create(ExceptionCode::AbortError, "IDBTransaction will abort due to uncaught exception in an event handler"_s));
}

void IDBRequest::setResult(const IDBKeyData& keyData)
{
    ASSERT(canCurrentThreadAccessThreadLocalData(originThread()));

    RefPtr context = scriptExecutionContext();
    if (!context)
        return;

    VM& vm = context->vm();
    JSLockHolder lock(vm);
    m_result = keyData;
    m_resultWrapper.clear();
}

void IDBRequest::setResult(const Vector<IDBKeyData>& keyDatas)
{
    ASSERT(canCurrentThreadAccessThreadLocalData(originThread()));

    RefPtr context = scriptExecutionContext();
    if (!context)
        return;

    VM& vm = context->vm();
    JSLockHolder lock(vm);
    m_result = keyDatas;
    m_resultWrapper.clear();
}

void IDBRequest::setResult(const IDBGetAllResult& result)
{
    ASSERT(canCurrentThreadAccessThreadLocalData(originThread()));

    RefPtr context = scriptExecutionContext();
    if (!context)
        return;

    VM& vm = context->vm();
    JSLockHolder lock(vm);
    m_result = result;
    m_resultWrapper.clear();
}

void IDBRequest::setResult(uint64_t number)
{
    ASSERT(canCurrentThreadAccessThreadLocalData(originThread()));

    RefPtr context = scriptExecutionContext();
    if (!context)
        return;

    VM& vm = context->vm();
    JSLockHolder lock(vm);
    m_result = number;
    m_resultWrapper.clear();
}

void IDBRequest::setResultToStructuredClone(const IDBGetResult& result)
{
    ASSERT(canCurrentThreadAccessThreadLocalData(originThread()));

    LOG(IndexedDB, "IDBRequest::setResultToStructuredClone");

    RefPtr context = scriptExecutionContext();
    if (!context)
        return;

    VM& vm = context->vm();
    JSLockHolder lock(vm);
    m_result = result;
    m_resultWrapper.clear();
}

void IDBRequest::setResultToUndefined()
{
    ASSERT(canCurrentThreadAccessThreadLocalData(originThread()));

    RefPtr context = scriptExecutionContext();
    if (!context)
        return;
    
    VM& vm = context->vm();
    JSLockHolder lock(vm);
    m_result = NullResultType::Undefined;
    m_resultWrapper.clear();
}

IDBCursor* IDBRequest::resultCursor()
{
    ASSERT(canCurrentThreadAccessThreadLocalData(originThread()));

    return WTF::switchOn(m_result,
        [] (const RefPtr<IDBCursor>& cursor) -> IDBCursor* { return cursor.get(); },
        [] (const auto&) -> IDBCursor* { return nullptr; }
    );
}

void IDBRequest::willIterateCursor(IDBCursor& cursor)
{
    ASSERT(canCurrentThreadAccessThreadLocalData(originThread()));
    ASSERT(isDone());
    ASSERT(scriptExecutionContext());
    ASSERT(m_transaction);
    ASSERT(!m_pendingCursor);
    ASSERT(&cursor == resultCursor());

    m_pendingCursor = cursor;
    m_result = NullResultType::Undefined;

    RefPtr context = scriptExecutionContext();
    if (!context)
        return;

    m_readyState = ReadyState::Pending;
    m_domError = nullptr;
    m_idbError = IDBError { };
}

void IDBRequest::didOpenOrIterateCursor(const IDBResultData& resultData)
{
    ASSERT(canCurrentThreadAccessThreadLocalData(originThread()));
    ASSERT(m_pendingCursor);

    RefPtr context = scriptExecutionContext();
    if (!context)
        return;

    VM& vm = context->vm();
    JSLockHolder lock(vm);

    m_result = NullResultType::Empty;
    if (resultData.type() == IDBResultType::IterateCursorSuccess || resultData.type() == IDBResultType::OpenCursorSuccess) {
        m_pendingCursor->setGetResult(*this, resultData.getResult(), m_currentTransactionOperationID);
        if (resultData.getResult().isDefined())
            m_result = m_pendingCursor;
    }

    if (std::get_if<NullResultType>(&m_result))
        m_resultWrapper.clear();

    m_pendingCursor = nullptr;

    completeRequestAndDispatchEvent(resultData);
}

void IDBRequest::completeRequestAndDispatchEvent(const IDBResultData& resultData)
{
    ASSERT(canCurrentThreadAccessThreadLocalData(originThread()));

    m_readyState = ReadyState::Done;

    m_idbError = resultData.error();
    if (!m_idbError.isNull())
        onError();
    else
        onSuccess();
}

void IDBRequest::onError()
{
    LOG(IndexedDB, "IDBRequest::onError");

    ASSERT(canCurrentThreadAccessThreadLocalData(originThread()));
    ASSERT(!m_idbError.isNull());

    m_domError = m_idbError.toDOMException();
    enqueueEvent(Event::create(eventNames().errorEvent, Event::CanBubble::Yes, Event::IsCancelable::Yes));
}

void IDBRequest::onSuccess()
{
    LOG(IndexedDB, "IDBRequest::onSuccess");
    ASSERT(canCurrentThreadAccessThreadLocalData(originThread()));
    enqueueEvent(Event::create(eventNames().successEvent, Event::CanBubble::No, Event::IsCancelable::No));
}

void IDBRequest::setResult(Ref<IDBDatabase>&& database)
{
    ASSERT(canCurrentThreadAccessThreadLocalData(originThread()));

    RefPtr context = scriptExecutionContext();
    if (!context)
        return;

    VM& vm = context->vm();
    JSLockHolder lock(vm);

    m_result = RefPtr<IDBDatabase> { WTFMove(database) };
    m_resultWrapper.clear();
}

void IDBRequest::clearWrappers()
{
    RefPtr context = scriptExecutionContext();
    if (!context)
        return;
    VM& vm = context->vm();
    JSLockHolder lock(vm);
    
    m_resultWrapper.clear();
    
    WTF::switchOn(m_result,
        [] (RefPtr<IDBCursor>& cursor) { cursor->clearWrappers(); },
        [] (const auto&) { }
    );
}

bool IDBRequest::willAbortTransactionAfterDispatchingEvent() const
{
    if (!m_eventBeingDispatched)
        return false;

    if (m_hasUncaughtException)
        return true;

    return !m_eventBeingDispatched->defaultPrevented() && m_eventBeingDispatched->type() == eventNames().errorEvent;
}

WebCoreOpaqueRoot root(IDBRequest* request)
{
    return WebCoreOpaqueRoot { request };
}

void IDBRequest::transactionTransitionedToFinishing()
{
    ASSERT(canCurrentThreadAccessThreadLocalData(originThread()));

    if (m_pendingActivity != PendingActivityType::CursorIteration)
        return;

    m_pendingActivity = PendingActivityType::None;
}

} // namespace WebCore
