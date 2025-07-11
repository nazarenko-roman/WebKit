/*
 *  Copyright (C) 2001 Peter Kelly (pmk@post.com)
 *  Copyright (C) 2003-2019 Apple Inc. All rights reserved.
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either
 *  version 2 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include "config.h"
#include "JSLazyEventListener.h"

#include "CachedScriptFetcher.h"
#include "ContentSecurityPolicy.h"
#include "DocumentInlines.h"
#include "Element.h"
#include "JSDOMWindow.h"
#include "JSDOMWindowBase.h"
#include "JSHTMLElement.h"
#include "LocalFrame.h"
#include "QualifiedName.h"
#include "SVGElement.h"
#include "ScriptController.h"
#include <JavaScriptCore/CatchScope.h>
#include <JavaScriptCore/FunctionConstructor.h>
#include <JavaScriptCore/IdentifierInlines.h>
#include <JavaScriptCore/SourceProvider.h>
#include <wtf/NeverDestroyed.h>
#include <wtf/RefCountedLeakCounter.h>
#include <wtf/StdLibExtras.h>
#include <wtf/WeakPtr.h>
#include <wtf/text/MakeString.h>

namespace WebCore {
using namespace JSC;

DEFINE_DEBUG_ONLY_GLOBAL(WTF::RefCountedLeakCounter, eventListenerCounter, ("JSLazyEventListener"));

struct JSLazyEventListener::CreationArguments {
    const QualifiedName& attributeName;
    const AtomString& attributeValue;
    Document& document;
    WeakPtr<ContainerNode, WeakPtrImplWithEventTargetData> node;
    JSObject* wrapper;
    bool shouldUseSVGEventName;
};

static const String& functionParameters(bool shouldUseSVGEventName)
{
    static NeverDestroyed<const String> eventString(MAKE_STATIC_STRING_IMPL("(event)"));
    static NeverDestroyed<const String> evtString(MAKE_STATIC_STRING_IMPL("(evt)"));
    return shouldUseSVGEventName ? evtString : eventString;
}

static TextPosition convertZeroToOne(const TextPosition& position)
{
    // A JSLazyEventListener can be created with a line number of zero when it is created with
    // a setAttribute call from JavaScript, so make the line number 1 in that case.
    if (position == TextPosition::belowRangePosition())
        return { };
    return position;
}

JSLazyEventListener::JSLazyEventListener(CreationArguments&& arguments, const URL& sourceURL, const TextPosition& sourcePosition)
    : JSEventListener(nullptr, arguments.wrapper, true, CreatedFromMarkup::Yes, mainThreadNormalWorldSingleton())
    , m_functionName(arguments.attributeName.localName().string())
    , m_functionParameters(functionParameters(arguments.shouldUseSVGEventName))
    , m_code(arguments.attributeValue)
    , m_sourceURL(sourceURL)
    , m_sourcePosition(convertZeroToOne(sourcePosition))
    , m_originalNode(WTFMove(arguments.node))
    , m_sourceTaintedOrigin(JSC::computeNewSourceTaintedOriginFromStack(arguments.document.vm(), arguments.document.vm().topCallFrame))
{
#ifndef NDEBUG
    eventListenerCounter.increment();
#endif
}

#if ASSERT_ENABLED
static inline bool isCloneInShadowTreeOfSVGUseElement(Node& originalNode, EventTarget& eventTarget)
{
    auto* element = dynamicDowncast<SVGElement>(eventTarget);
    if (!element || !element->correspondingElement())
        return false;

    ASSERT(element->isInShadowTree());
    return &originalNode == element->correspondingElement();
}

// This is to help find the underlying cause of <rdar://problem/24314027>.
void JSLazyEventListener::checkValidityForEventTarget(EventTarget& eventTarget)
{
    if (eventTarget.isNode()) {
        ASSERT(m_originalNode);
        ASSERT(static_cast<EventTarget*>(m_originalNode.get()) == &eventTarget || isCloneInShadowTreeOfSVGUseElement(*m_originalNode, eventTarget));
    } else
        ASSERT(!m_originalNode);
}
#endif

JSLazyEventListener::~JSLazyEventListener()
{
#ifndef NDEBUG
    eventListenerCounter.decrement();
#endif
}

JSObject* JSLazyEventListener::initializeJSFunction(ScriptExecutionContext& executionContext) const
{
    Ref executionContextDocument = downcast<Document>(executionContext);

    // As per the HTML specification [1], if this is an element's event handler, then document should be the
    // element's document. The script execution context may be different from the node's document if the
    // node's document was created by JavaScript.
    // [1] https://html.spec.whatwg.org/multipage/webappapis.html#getting-the-current-value-of-the-event-handler
    Ref document = m_originalNode ? m_originalNode->document() : executionContextDocument.get();
    if (!document->frame())
        return nullptr;

    RefPtr element = dynamicDowncast<Element>(m_originalNode.get());
    if (!document->checkedContentSecurityPolicy()->allowInlineEventHandlers(m_sourceURL.string(), m_sourcePosition.m_line, m_code, element.get()))
        return nullptr;

    RefPtr frame = document->frame();
    CheckedRef script = frame->script();
    if (!script->canExecuteScripts(ReasonForCallingCanExecuteScripts::AboutToCreateEventListener) || script->isPaused())
        return nullptr;

    ASSERT_WITH_MESSAGE(document->settings().scriptMarkupEnabled(), "Scripting element attributes should have been stripped during parsing");
    if (!document->settings().scriptMarkupEnabled()) [[unlikely]]
        return nullptr;

    if (!executionContextDocument->frame())
        return nullptr;

    RefPtr isolatedWorld = this->isolatedWorld();
    if (!isolatedWorld) [[unlikely]]
        return nullptr;

    auto* globalObject = toJSDOMWindow(*executionContextDocument->protectedFrame(), *isolatedWorld);
    if (!globalObject)
        return nullptr;

    VM& vm = globalObject->vm();
    JSLockHolder lock(vm);
    auto scope = DECLARE_CATCH_SCOPE(vm);
    JSGlobalObject* lexicalGlobalObject = globalObject;

    static NeverDestroyed<const String> functionPrefix(MAKE_STATIC_STRING_IMPL("function "));
    int functionConstructorParametersEndPosition = functionPrefix->length() + m_functionName.length() + m_functionParameters.length();
    String code = makeString(functionPrefix.get(), m_functionName, m_functionParameters, " {\n"_s, m_code, "\n}"_s);

    bool listenerHasEventHandlerScope = is<HTMLElement>(m_originalNode.get());
    LexicallyScopedFeatures lexicallyScopedFeatures = listenerHasEventHandlerScope || globalObject->globalScopeExtension() ? TaintedByWithScopeLexicallyScopedFeature : NoLexicallyScopedFeatures;

    // We want all errors to refer back to the line on which our attribute was
    // declared, regardless of any newlines in our JavaScript source text.
    int overrideLineNumber = m_sourcePosition.m_line.oneBasedInt();

    JSObject* jsFunction = constructFunctionSkippingEvalEnabledCheck(
        lexicalGlobalObject, WTFMove(code), lexicallyScopedFeatures, Identifier::fromString(vm, m_functionName),
        SourceOrigin { m_sourceURL, CachedScriptFetcher::create(document->charset()) },
        m_sourceURL.string(), m_sourceTaintedOrigin, m_sourcePosition, overrideLineNumber, functionConstructorParametersEndPosition);
    if (scope.exception()) [[unlikely]] {
        reportCurrentException(lexicalGlobalObject);
        scope.clearException();
        return nullptr;
    }

    if (m_originalNode) {
        if (!wrapper()) {
            // Ensure that 'node' has a JavaScript wrapper to mark the event listener we're creating.
            // FIXME: Should pass the global object associated with the node
            setWrapperWhenInitializingJSFunction(vm, asObject(toJS(lexicalGlobalObject, globalObject, *m_originalNode)));
        }

        if (listenerHasEventHandlerScope) {
            ASSERT(wrapper()->inherits<JSHTMLElement>());
            // Add the event's home element to the scope (and the document, and the form - see JSHTMLElement::eventHandlerScope)
            JSFunction* listenerAsFunction = jsCast<JSFunction*>(jsFunction);
            listenerAsFunction->setScope(vm, jsCast<JSHTMLElement*>(wrapper())->pushEventHandlerScope(lexicalGlobalObject, listenerAsFunction->scope()));
        }
    }

    return jsFunction;
}

RefPtr<JSLazyEventListener> JSLazyEventListener::create(CreationArguments&& arguments)
{
    if (arguments.attributeValue.isNull())
        return nullptr;

    // FIXME: We should be able to provide source information for frameless documents too (e.g. for importing nodes from XMLHttpRequest.responseXML).
    TextPosition position;
    URL sourceURL;
    if (auto* frame = arguments.document.frame()) {
        if (!frame->script().canExecuteScripts(ReasonForCallingCanExecuteScripts::AboutToCreateEventListener))
            return nullptr;
        position = frame->script().eventHandlerPosition();
        sourceURL = arguments.document.url();
    }

    JSLockHolder locker(arguments.document.vm());
    return adoptRef(*new JSLazyEventListener(WTFMove(arguments), sourceURL, position));
}

RefPtr<JSLazyEventListener> JSLazyEventListener::create(Element& element, const QualifiedName& attributeName, const AtomString& attributeValue)
{
    return create({ attributeName, attributeValue, element.document(), element, nullptr, element.isSVGElement() });
}

RefPtr<JSLazyEventListener> JSLazyEventListener::create(Document& document, const QualifiedName& attributeName, const AtomString& attributeValue)
{
    // FIXME: This always passes false for "shouldUseSVGEventName". Is that correct for events dispatched to SVG documents?
    // This has been this way for a long time, but became more obvious when refactoring to separate the Element and Document code paths.
    return create({ attributeName, attributeValue, document, document, nullptr, false });
}

RefPtr<JSLazyEventListener> JSLazyEventListener::create(LocalDOMWindow& window, const QualifiedName& attributeName, const AtomString& attributeValue)
{
    ASSERT(window.document());
    auto& document = *window.document();
    ASSERT(document.frame());
    return create({ attributeName, attributeValue, document, nullptr, toJSDOMWindow(document.frame(), mainThreadNormalWorldSingleton()), document.isSVGDocument() });
}

} // namespace WebCore
