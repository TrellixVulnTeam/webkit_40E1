/*
 * Copyright (C) 1999 Lars Knoll (knoll@kde.org)
 *           (C) 1999 Antti Koivisto (koivisto@kde.org)
 *           (C) 2001 Dirk Mueller (mueller@kde.org)
 * Copyright (C) 2004-2017 Apple Inc. All rights reserved.
 *           (C) 2006 Alexey Proskuryakov (ap@nypop.com)
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * along with this library; see the file COPYING.LIB.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 *
 */

#include "config.h"
#include "HTMLTextFormControlElement.h"

#include "AXObjectCache.h"
#include "CSSPrimitiveValueMappings.h"
#include "ChromeClient.h"
#include "Document.h"
#include "Editing.h"
#include "Event.h"
#include "EventNames.h"
#include "ExceptionCode.h"
#include "Frame.h"
#include "FrameSelection.h"
#include "HTMLBRElement.h"
#include "HTMLFormElement.h"
#include "HTMLInputElement.h"
#include "HTMLNames.h"
#include "HTMLParserIdioms.h"
#include "Logging.h"
#include "NoEventDispatchAssertion.h"
#include "NodeTraversal.h"
#include "Page.h"
#include "RenderTextControlSingleLine.h"
#include "RenderTheme.h"
#include "ShadowRoot.h"
#include "Text.h"
#include "TextControlInnerElements.h"
#include <wtf/text/StringBuilder.h>

namespace WebCore {

using namespace HTMLNames;

static Position positionForIndex(TextControlInnerTextElement*, unsigned);

HTMLTextFormControlElement::HTMLTextFormControlElement(const QualifiedName& tagName, Document& document, HTMLFormElement* form)
    : HTMLFormControlElementWithState(tagName, document, form)
    , m_cachedSelectionDirection(SelectionHasNoDirection)
    , m_lastChangeWasUserEdit(false)
    , m_isPlaceholderVisible(false)
    , m_cachedSelectionStart(-1)
    , m_cachedSelectionEnd(-1)
{
}

HTMLTextFormControlElement::~HTMLTextFormControlElement()
{
}

bool HTMLTextFormControlElement::childShouldCreateRenderer(const Node& child) const
{
    // FIXME: We shouldn't force the pseudo elements down into the shadow, but
    // this perserves the current behavior of WebKit.
    if (child.isPseudoElement())
        return HTMLFormControlElementWithState::childShouldCreateRenderer(child);
    return hasShadowRootParent(child) && HTMLFormControlElementWithState::childShouldCreateRenderer(child);
}

Node::InsertionNotificationRequest HTMLTextFormControlElement::insertedInto(ContainerNode& insertionPoint)
{
    InsertionNotificationRequest insertionNotificationRequest = HTMLFormControlElementWithState::insertedInto(insertionPoint);
    if (!insertionPoint.isConnected())
        return insertionNotificationRequest;
    String initialValue = value();
    setTextAsOfLastFormControlChangeEvent(initialValue.isNull() ? emptyString() : initialValue);
    return insertionNotificationRequest;
}

void HTMLTextFormControlElement::dispatchFocusEvent(RefPtr<Element>&& oldFocusedElement, FocusDirection direction)
{
    if (supportsPlaceholder())
        updatePlaceholderVisibility();
    handleFocusEvent(oldFocusedElement.get(), direction);
    HTMLFormControlElementWithState::dispatchFocusEvent(WTFMove(oldFocusedElement), direction);
}

void HTMLTextFormControlElement::dispatchBlurEvent(RefPtr<Element>&& newFocusedElement)
{
    if (supportsPlaceholder())
        updatePlaceholderVisibility();
    // Match the order in Document::setFocusedElement.
    handleBlurEvent();
    HTMLFormControlElementWithState::dispatchBlurEvent(WTFMove(newFocusedElement));
}

void HTMLTextFormControlElement::didEditInnerTextValue()
{
    if (!isTextFormControl())
        return;

    LOG(Editing, "HTMLTextFormControlElement %p didEditInnerTextValue", this);

    m_lastChangeWasUserEdit = true;
    subtreeHasChanged();
}

void HTMLTextFormControlElement::forwardEvent(Event& event)
{
    if (event.type() == eventNames().blurEvent || event.type() == eventNames().focusEvent)
        return;
    innerTextElement()->defaultEventHandler(event);
}

String HTMLTextFormControlElement::strippedPlaceholder() const
{
    // According to the HTML5 specification, we need to remove CR and LF from
    // the attribute value.
    const AtomicString& attributeValue = attributeWithoutSynchronization(placeholderAttr);
    if (!attributeValue.contains(newlineCharacter) && !attributeValue.contains(carriageReturn))
        return attributeValue;

    StringBuilder stripped;
    unsigned length = attributeValue.length();
    stripped.reserveCapacity(length);
    for (unsigned i = 0; i < length; ++i) {
        UChar character = attributeValue[i];
        if (character == newlineCharacter || character == carriageReturn)
            continue;
        stripped.append(character);
    }
    return stripped.toString();
}

static bool isNotLineBreak(UChar ch) { return ch != newlineCharacter && ch != carriageReturn; }

bool HTMLTextFormControlElement::isPlaceholderEmpty() const
{
    const AtomicString& attributeValue = attributeWithoutSynchronization(placeholderAttr);
    return attributeValue.string().find(isNotLineBreak) == notFound;
}

bool HTMLTextFormControlElement::placeholderShouldBeVisible() const
{
    // This function is used by the style resolver to match the :placeholder-shown pseudo class.
    // Since it is used for styling, it must not use any value depending on the style.
    return supportsPlaceholder() && isEmptyValue() && !isPlaceholderEmpty();
}

void HTMLTextFormControlElement::updatePlaceholderVisibility()
{
    bool placeHolderWasVisible = m_isPlaceholderVisible;
    m_isPlaceholderVisible = placeholderShouldBeVisible();

    if (placeHolderWasVisible == m_isPlaceholderVisible)
        return;

    invalidateStyleForSubtree();
}

void HTMLTextFormControlElement::setSelectionStart(int start)
{
    setSelectionRange(start, std::max(start, selectionEnd()), selectionDirection());
}

void HTMLTextFormControlElement::setSelectionEnd(int end)
{
    setSelectionRange(std::min(end, selectionStart()), end, selectionDirection());
}

void HTMLTextFormControlElement::setSelectionDirection(const String& direction)
{
    setSelectionRange(selectionStart(), selectionEnd(), direction);
}

void HTMLTextFormControlElement::select(const AXTextStateChangeIntent& intent)
{
    // FIXME: We should abstract the selection behavior into an EditingBehavior function instead
    // of hardcoding the behavior using a macro define.
#if PLATFORM(IOS)
    // We don't want to select all the text on iOS. Instead use the standard textfield behavior of going to the end of the line.
    setSelectionRange(std::numeric_limits<int>::max(), std::numeric_limits<int>::max(), SelectionHasForwardDirection, intent);
#else
    setSelectionRange(0, std::numeric_limits<int>::max(), SelectionHasNoDirection, intent);
#endif
}

String HTMLTextFormControlElement::selectedText() const
{
    if (!isTextFormControl())
        return String();
    return value().substring(selectionStart(), selectionEnd() - selectionStart());
}

void HTMLTextFormControlElement::dispatchFormControlChangeEvent()
{
    if (m_textAsOfLastFormControlChangeEvent != value()) {
        dispatchChangeEvent();
        setTextAsOfLastFormControlChangeEvent(value());
    }
    setChangedSinceLastFormControlChangeEvent(false);
}

ExceptionOr<void> HTMLTextFormControlElement::setRangeText(const String& replacement)
{
    return setRangeText(replacement, selectionStart(), selectionEnd(), String());
}

ExceptionOr<void> HTMLTextFormControlElement::setRangeText(const String& replacement, unsigned start, unsigned end, const String& selectionMode)
{
    if (start > end)
        return Exception { INDEX_SIZE_ERR };

    String text = innerTextValue();
    unsigned textLength = text.length();
    unsigned replacementLength = replacement.length();
    unsigned newSelectionStart = selectionStart();
    unsigned newSelectionEnd = selectionEnd();

    start = std::min(start, textLength);
    end = std::min(end, textLength);

    if (start < end)
        text.replace(start, end - start, replacement);
    else
        text.insert(replacement, start);

    setInnerTextValue(text);

    // FIXME: What should happen to the value (as in value()) if there's no renderer?
    if (!renderer())
        return { };

    subtreeHasChanged();

    if (equalLettersIgnoringASCIICase(selectionMode, "select")) {
        newSelectionStart = start;
        newSelectionEnd = start + replacementLength;
    } else if (equalLettersIgnoringASCIICase(selectionMode, "start"))
        newSelectionStart = newSelectionEnd = start;
    else if (equalLettersIgnoringASCIICase(selectionMode, "end"))
        newSelectionStart = newSelectionEnd = start + replacementLength;
    else {
        // Default is "preserve".
        long delta = replacementLength - (end - start);

        if (newSelectionStart > end)
            newSelectionStart += delta;
        else if (newSelectionStart > start)
            newSelectionStart = start;

        if (newSelectionEnd > end)
            newSelectionEnd += delta;
        else if (newSelectionEnd > start)
            newSelectionEnd = start + replacementLength;
    }

    setSelectionRange(newSelectionStart, newSelectionEnd, SelectionHasNoDirection);

    return { };
}

void HTMLTextFormControlElement::setSelectionRange(int start, int end, const String& directionString, const AXTextStateChangeIntent& intent)
{
    TextFieldSelectionDirection direction = SelectionHasNoDirection;
    if (directionString == "forward")
        direction = SelectionHasForwardDirection;
    else if (directionString == "backward")
        direction = SelectionHasBackwardDirection;

    return setSelectionRange(start, end, direction, intent);
}

void HTMLTextFormControlElement::setSelectionRange(int start, int end, TextFieldSelectionDirection direction, const AXTextStateChangeIntent& intent)
{
    if (!isTextFormControl())
        return;

    end = std::max(end, 0);
    start = std::min(std::max(start, 0), end);

    TextControlInnerTextElement* innerText = innerTextElement();
    bool hasFocus = document().focusedElement() == this;
    if (!hasFocus && innerText) {
        // FIXME: Removing this synchronous layout requires fixing <https://webkit.org/b/128797>
        document().updateLayoutIgnorePendingStylesheets();

        // Double-check the state of innerTextElement after the layout.
        innerText = innerTextElement();
        auto* rendererTextControl = renderer();

        if (innerText && rendererTextControl) {
            if (rendererTextControl->style().visibility() == HIDDEN || !innerText->renderBox()->height()) {
                cacheSelection(start, end, direction);
                return;
            }
        }
    }

    Position startPosition = positionForIndex(innerText, start);
    Position endPosition;
    if (start == end)
        endPosition = startPosition;
    else {
        if (direction == SelectionHasBackwardDirection) {
            endPosition = startPosition;
            startPosition = positionForIndex(innerText, end);
        } else
            endPosition = positionForIndex(innerText, end);
    }

    if (Frame* frame = document().frame())
        frame->selection().moveWithoutValidationTo(startPosition, endPosition, direction != SelectionHasNoDirection, !hasFocus, intent);
}

int HTMLTextFormControlElement::indexForVisiblePosition(const VisiblePosition& position) const
{
    TextControlInnerTextElement* innerText = innerTextElement();
    if (!innerText || !innerText->contains(position.deepEquivalent().anchorNode()))
        return 0;
    unsigned index = indexForPosition(position.deepEquivalent());
    ASSERT(VisiblePosition(positionForIndex(innerTextElement(), index)) == position);
    return index;
}

VisiblePosition HTMLTextFormControlElement::visiblePositionForIndex(int index) const
{
    VisiblePosition position = positionForIndex(innerTextElement(), index);
    ASSERT(indexForVisiblePosition(position) == index);
    return position;
}

int HTMLTextFormControlElement::selectionStart() const
{
    if (!isTextFormControl())
        return 0;
    if (document().focusedElement() != this && hasCachedSelection())
        return m_cachedSelectionStart;

    return computeSelectionStart();
}

int HTMLTextFormControlElement::computeSelectionStart() const
{
    ASSERT(isTextFormControl());
    Frame* frame = document().frame();
    if (!frame)
        return 0;

    return indexForPosition(frame->selection().selection().start());
}

int HTMLTextFormControlElement::selectionEnd() const
{
    if (!isTextFormControl())
        return 0;
    if (document().focusedElement() != this && hasCachedSelection())
        return m_cachedSelectionEnd;
    return computeSelectionEnd();
}

int HTMLTextFormControlElement::computeSelectionEnd() const
{
    ASSERT(isTextFormControl());
    Frame* frame = document().frame();
    if (!frame)
        return 0;

    return indexForPosition(frame->selection().selection().end());
}

static const AtomicString& directionString(TextFieldSelectionDirection direction)
{
    static NeverDestroyed<const AtomicString> none("none", AtomicString::ConstructFromLiteral);
    static NeverDestroyed<const AtomicString> forward("forward", AtomicString::ConstructFromLiteral);
    static NeverDestroyed<const AtomicString> backward("backward", AtomicString::ConstructFromLiteral);

    switch (direction) {
    case SelectionHasNoDirection:
        return none;
    case SelectionHasForwardDirection:
        return forward;
    case SelectionHasBackwardDirection:
        return backward;
    }

    ASSERT_NOT_REACHED();
    return none;
}

const AtomicString& HTMLTextFormControlElement::selectionDirection() const
{
    if (!isTextFormControl())
        return directionString(SelectionHasNoDirection);
    if (document().focusedElement() != this && hasCachedSelection())
        return directionString(cachedSelectionDirection());

    return directionString(computeSelectionDirection());
}

TextFieldSelectionDirection HTMLTextFormControlElement::computeSelectionDirection() const
{
    ASSERT(isTextFormControl());
    Frame* frame = document().frame();
    if (!frame)
        return SelectionHasNoDirection;

    const VisibleSelection& selection = frame->selection().selection();
    return selection.isDirectional() ? (selection.isBaseFirst() ? SelectionHasForwardDirection : SelectionHasBackwardDirection) : SelectionHasNoDirection;
}

static inline void setContainerAndOffsetForRange(Node* node, int offset, Node*& containerNode, int& offsetInContainer)
{
    if (node->isTextNode()) {
        containerNode = node;
        offsetInContainer = offset;
    } else {
        containerNode = node->parentNode();
        offsetInContainer = node->computeNodeIndex() + offset;
    }
}

RefPtr<Range> HTMLTextFormControlElement::selection() const
{
    if (!renderer() || !isTextFormControl() || !hasCachedSelection())
        return nullptr;

    int start = m_cachedSelectionStart;
    int end = m_cachedSelectionEnd;

    ASSERT(start <= end);
    TextControlInnerTextElement* innerText = innerTextElement();
    if (!innerText)
        return nullptr;

    if (!innerText->firstChild())
        return Range::create(document(), innerText, 0, innerText, 0);

    int offset = 0;
    Node* startNode = nullptr;
    Node* endNode = nullptr;
    for (Node* node = innerText->firstChild(); node; node = NodeTraversal::next(*node, innerText)) {
        ASSERT(!node->firstChild());
        ASSERT(node->isTextNode() || node->hasTagName(brTag));
        int length = node->isTextNode() ? lastOffsetInNode(node) : 1;

        if (offset <= start && start <= offset + length)
            setContainerAndOffsetForRange(node, start - offset, startNode, start);

        if (offset <= end && end <= offset + length) {
            setContainerAndOffsetForRange(node, end - offset, endNode, end);
            break;
        }

        offset += length;
    }

    if (!startNode || !endNode)
        return nullptr;

    return Range::create(document(), startNode, start, endNode, end);
}

void HTMLTextFormControlElement::restoreCachedSelection(const AXTextStateChangeIntent& intent)
{
    setSelectionRange(m_cachedSelectionStart, m_cachedSelectionEnd, cachedSelectionDirection(), intent);
}

void HTMLTextFormControlElement::selectionChanged(bool shouldFireSelectEvent)
{
    if (!isTextFormControl())
        return;

    // FIXME: Don't re-compute selection start and end if this function was called inside setSelectionRange.
    // selectionStart() or selectionEnd() will return cached selection when this node doesn't have focus
    cacheSelection(computeSelectionStart(), computeSelectionEnd(), computeSelectionDirection());
    
    if (shouldFireSelectEvent && m_cachedSelectionStart != m_cachedSelectionEnd)
        dispatchEvent(Event::create(eventNames().selectEvent, true, false));
}

void HTMLTextFormControlElement::parseAttribute(const QualifiedName& name, const AtomicString& value)
{
    if (name == placeholderAttr) {
        updatePlaceholderText();
        updatePlaceholderVisibility();
    } else
        HTMLFormControlElementWithState::parseAttribute(name, value);
}

void HTMLTextFormControlElement::disabledStateChanged()
{
    HTMLFormControlElementWithState::disabledStateChanged();
    updateInnerTextElementEditability();
}

void HTMLTextFormControlElement::readOnlyAttributeChanged()
{
    HTMLFormControlElementWithState::disabledAttributeChanged();
    updateInnerTextElementEditability();
}

void HTMLTextFormControlElement::updateInnerTextElementEditability()
{
    if (TextControlInnerTextElement* innerText = innerTextElement())
        innerText->setAttributeWithoutSynchronization(contenteditableAttr, isDisabledOrReadOnly() ? "false" : "plaintext-only");
}

bool HTMLTextFormControlElement::lastChangeWasUserEdit() const
{
    if (!isTextFormControl())
        return false;
    return m_lastChangeWasUserEdit;
}

static void stripTrailingNewline(StringBuilder& result)
{
    // Remove one trailing newline; there's always one that's collapsed out by rendering.
    size_t size = result.length();
    if (size && result[size - 1] == newlineCharacter)
        result.resize(size - 1);
}

static String innerTextValueFrom(TextControlInnerTextElement& innerText)
{
    StringBuilder result;
    for (Node* node = innerText.firstChild(); node; node = NodeTraversal::next(*node, &innerText)) {
        if (is<HTMLBRElement>(*node))
            result.append(newlineCharacter);
        else if (is<Text>(*node))
            result.append(downcast<Text>(*node).data());
    }
    stripTrailingNewline(result);
    return result.toString();
}

void HTMLTextFormControlElement::setInnerTextValue(const String& value)
{
    TextControlInnerTextElement* innerText = innerTextElement();
    if (!innerText)
        return;

    ASSERT(isTextFormControl());
    String previousValue = innerTextValueFrom(*innerText);
    bool textIsChanged = value != previousValue;
    if (textIsChanged || !innerText->hasChildNodes()) {
#if HAVE(ACCESSIBILITY) && !PLATFORM(COCOA)
        if (textIsChanged && renderer()) {
            if (AXObjectCache* cache = document().existingAXObjectCache())
                cache->postNotification(this, AXObjectCache::AXValueChanged, TargetObservableParent);
        }
#endif

        {
            // Events dispatched on the inner text element cannot execute arbitrary author scripts.
            NoEventDispatchAssertion::EventAllowedScope allowedScope(*userAgentShadowRoot());

            innerText->setInnerText(value);

            if (value.endsWith('\n') || value.endsWith('\r'))
                innerText->appendChild(HTMLBRElement::create(document()));
        }

#if HAVE(ACCESSIBILITY) && PLATFORM(COCOA)
        if (textIsChanged && renderer()) {
            if (AXObjectCache* cache = document().existingAXObjectCache())
                cache->postTextReplacementNotification(this, AXTextEditTypeDelete, previousValue, AXTextEditTypeInsert, value, VisiblePosition(Position(this, Position::PositionIsBeforeAnchor)));
        }
#endif
    }

    setFormControlValueMatchesRenderer(true);
}

String HTMLTextFormControlElement::innerTextValue() const
{
    TextControlInnerTextElement* innerText = innerTextElement();
    return innerText ? innerTextValueFrom(*innerText) : emptyString();
}

static Position positionForIndex(TextControlInnerTextElement* innerText, unsigned index)
{
    unsigned remainingCharactersToMoveForward = index;
    Node* lastBrOrText = innerText;
    for (Node* node = innerText; node; node = NodeTraversal::next(*node, innerText)) {
        if (node->hasTagName(brTag)) {
            if (!remainingCharactersToMoveForward)
                return positionBeforeNode(node);
            remainingCharactersToMoveForward--;
            lastBrOrText = node;
        } else if (is<Text>(*node)) {
            Text& text = downcast<Text>(*node);
            if (remainingCharactersToMoveForward < text.length())
                return Position(&text, remainingCharactersToMoveForward);
            remainingCharactersToMoveForward -= text.length();
            lastBrOrText = node;
        }
    }
    return lastPositionInOrAfterNode(lastBrOrText);
}

unsigned HTMLTextFormControlElement::indexForPosition(const Position& passedPosition) const
{
    TextControlInnerTextElement* innerText = innerTextElement();
    if (!innerText || !innerText->contains(passedPosition.anchorNode()) || passedPosition.isNull())
        return 0;

    if (positionBeforeNode(innerText) == passedPosition)
        return 0;

    unsigned index = 0;
    Node* startNode = passedPosition.computeNodeBeforePosition();
    if (!startNode)
        startNode = passedPosition.containerNode();
    ASSERT(startNode);
    ASSERT(innerText->contains(startNode));

    for (Node* node = startNode; node; node = NodeTraversal::previous(*node, innerText)) {
        if (is<Text>(*node)) {
            unsigned length = downcast<Text>(*node).length();
            if (node == passedPosition.containerNode())
                index += std::min<unsigned>(length, passedPosition.offsetInContainerNode());
            else
                index += length;
        } else if (is<HTMLBRElement>(*node))
            ++index;
    }

    unsigned length = innerTextValue().length();
    index = std::min(index, length); // FIXME: We shouldn't have to call innerTextValue() just to ignore the last LF. See finishText.
#ifndef ASSERT_DISABLED
    VisiblePosition visiblePosition = passedPosition;
    unsigned indexComputedByVisiblePosition = 0;
    if (visiblePosition.isNotNull())
        indexComputedByVisiblePosition = WebCore::indexForVisiblePosition(innerText, visiblePosition, false /* forSelectionPreservation */);
    ASSERT(index == indexComputedByVisiblePosition);
#endif
    return index;
}

#if PLATFORM(IOS)
void HTMLTextFormControlElement::hidePlaceholder()
{
    if (HTMLElement* placeholder = placeholderElement())
        placeholder->setInlineStyleProperty(CSSPropertyVisibility, CSSValueHidden, true);
}

void HTMLTextFormControlElement::showPlaceholderIfNecessary()
{
    if (HTMLElement* placeholder = placeholderElement())
        placeholder->setInlineStyleProperty(CSSPropertyVisibility, CSSValueVisible, true);
}
#endif

static void getNextSoftBreak(RootInlineBox*& line, Node*& breakNode, unsigned& breakOffset)
{
    RootInlineBox* next;
    for (; line; line = next) {
        next = line->nextRootBox();
        if (next && !line->endsWithBreak()) {
            ASSERT(line->lineBreakObj());
            breakNode = line->lineBreakObj()->node();
            breakOffset = line->lineBreakPos();
            line = next;
            return;
        }
    }
    breakNode = 0;
    breakOffset = 0;
}

String HTMLTextFormControlElement::valueWithHardLineBreaks() const
{
    // FIXME: It's not acceptable to ignore the HardWrap setting when there is no renderer.
    // While we have no evidence this has ever been a practical problem, it would be best to fix it some day.
    if (!isTextFormControl())
        return value();

    TextControlInnerTextElement* innerText = innerTextElement();
    if (!innerText)
        return value();

    RenderTextControlInnerBlock* renderer = innerText->renderer();
    if (!renderer)
        return value();

    Node* breakNode;
    unsigned breakOffset;
    RootInlineBox* line = renderer->firstRootBox();
    if (!line)
        return value();

    getNextSoftBreak(line, breakNode, breakOffset);

    StringBuilder result;
    for (Node* node = innerText->firstChild(); node; node = NodeTraversal::next(*node, innerText)) {
        if (is<HTMLBRElement>(*node))
            result.append(newlineCharacter);
        else if (is<Text>(*node)) {
            String data = downcast<Text>(*node).data();
            unsigned length = data.length();
            unsigned position = 0;
            while (breakNode == node && breakOffset <= length) {
                if (breakOffset > position) {
                    result.append(data, position, breakOffset - position);
                    position = breakOffset;
                    result.append(newlineCharacter);
                }
                getNextSoftBreak(line, breakNode, breakOffset);
            }
            result.append(data, position, length - position);
        }
        while (breakNode == node)
            getNextSoftBreak(line, breakNode, breakOffset);
    }
    stripTrailingNewline(result);
    return result.toString();
}

HTMLTextFormControlElement* enclosingTextFormControl(const Position& position)
{
    ASSERT(position.isNull() || position.anchorType() == Position::PositionIsOffsetInAnchor
        || position.containerNode() || !position.anchorNode()->shadowHost()
        || hasShadowRootParent(*position.anchorNode()));
        
    Node* container = position.containerNode();
    if (!container)
        return nullptr;
    Element* ancestor = container->shadowHost();
    return ancestor && is<HTMLTextFormControlElement>(*ancestor) ? downcast<HTMLTextFormControlElement>(ancestor) : nullptr;
}

static const Element* parentHTMLElement(const Element* element)
{
    while (element) {
        element = element->parentElement();
        if (element && element->isHTMLElement())
            return element;
    }
    return 0;
}

String HTMLTextFormControlElement::directionForFormData() const
{
    for (const Element* element = this; element; element = parentHTMLElement(element)) {
        const AtomicString& dirAttributeValue = element->attributeWithoutSynchronization(dirAttr);
        if (dirAttributeValue.isNull())
            continue;

        if (equalLettersIgnoringASCIICase(dirAttributeValue, "rtl") || equalLettersIgnoringASCIICase(dirAttributeValue, "ltr"))
            return dirAttributeValue;

        if (equalLettersIgnoringASCIICase(dirAttributeValue, "auto")) {
            bool isAuto;
            TextDirection textDirection = static_cast<const HTMLElement*>(element)->directionalityIfhasDirAutoAttribute(isAuto);
            return textDirection == RTL ? "rtl" : "ltr";
        }
    }

    return "ltr";
}

ExceptionOr<void> HTMLTextFormControlElement::setMaxLength(int maxLength)
{
    if (maxLength < 0 || (m_minLength >= 0 && maxLength < m_minLength))
        return Exception { INDEX_SIZE_ERR };
    setIntegralAttribute(maxlengthAttr, maxLength);
    return { };
}

ExceptionOr<void> HTMLTextFormControlElement::setMinLength(int minLength)
{
    if (minLength < 0 || (m_maxLength >= 0 && minLength > m_maxLength))
        return Exception { INDEX_SIZE_ERR };
    setIntegralAttribute(minlengthAttr, minLength);
    return { };
}

void HTMLTextFormControlElement::adjustInnerTextStyle(const RenderStyle& parentStyle, RenderStyle& textBlockStyle) const
{
    // The inner block, if present, always has its direction set to LTR,
    // so we need to inherit the direction and unicode-bidi style from the element.
    textBlockStyle.setDirection(parentStyle.direction());
    textBlockStyle.setUnicodeBidi(parentStyle.unicodeBidi());

    if (HTMLElement* innerText = innerTextElement()) {
        if (const StyleProperties* properties = innerText->presentationAttributeStyle()) {
            RefPtr<CSSValue> value = properties->getPropertyCSSValue(CSSPropertyWebkitUserModify);
            if (is<CSSPrimitiveValue>(value.get()))
                textBlockStyle.setUserModify(downcast<CSSPrimitiveValue>(*value));
        }
    }

    if (isDisabledFormControl())
        textBlockStyle.setColor(document().page()->theme().disabledTextColor(textBlockStyle.visitedDependentColor(CSSPropertyColor), parentStyle.visitedDependentColor(CSSPropertyBackgroundColor)));
#if PLATFORM(IOS)
    if (textBlockStyle.textSecurity() != TSNONE && !textBlockStyle.isLeftToRightDirection()) {
        // Preserve the alignment but force the direction to LTR so that the last-typed, unmasked character
        // (which cannot have RTL directionality) will appear to the right of the masked characters. See <rdar://problem/7024375>.
        
        switch (textBlockStyle.textAlign()) {
        case TASTART:
        case JUSTIFY:
            textBlockStyle.setTextAlign(RIGHT);
            break;
        case TAEND:
            textBlockStyle.setTextAlign(LEFT);
            break;
        case LEFT:
        case RIGHT:
        case CENTER:
        case WEBKIT_LEFT:
        case WEBKIT_RIGHT:
        case WEBKIT_CENTER:
            break;
        }

        textBlockStyle.setDirection(LTR);
    }
#endif
}

} // namespace Webcore
