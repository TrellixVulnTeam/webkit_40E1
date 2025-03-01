/*
 * Copyright (C) 2006, 2007, 2008 Apple Inc. All rights reserved.
 * Copyright (C) 2009 Torch Mobile Inc. All rights reserved. (http://www.torchmobile.com/)
 *
 * Portions are Copyright (C) 1998 Netscape Communications Corporation.
 *
 * Other contributors:
 *   Robert O'Callahan <roc+@cs.cmu.edu>
 *   David Baron <dbaron@fas.harvard.edu>
 *   Christian Biesinger <cbiesinger@web.de>
 *   Randall Jesup <rjesup@wgate.com>
 *   Roland Mainz <roland.mainz@informatik.med.uni-giessen.de>
 *   Josh Soref <timeless@mac.com>
 *   Boris Zbarsky <bzbarsky@mit.edu>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * Alternatively, the contents of this file may be used under the terms
 * of either the Mozilla Public License Version 1.1, found at
 * http://www.mozilla.org/MPL/ (the "MPL") or the GNU General Public
 * License Version 2.0, found at http://www.fsf.org/copyleft/gpl.html
 * (the "GPL"), in which case the provisions of the MPL or the GPL are
 * applicable instead of those above.  If you wish to allow use of your
 * version of this file only under the terms of one of those two
 * licenses (the MPL or the GPL) and not to allow others to use your
 * version of this file under the LGPL, indicate your decision by
 * deletingthe provisions above and replace them with the notice and
 * other provisions required by the MPL or the GPL, as the case may be.
 * If you do not delete the provisions above, a recipient may use your
 * version of this file under any of the LGPL, the MPL or the GPL.
 */

#include "config.h"

#include "RenderMarquee.h"

#include "FrameView.h"
#include "HTMLMarqueeElement.h"
#include "HTMLNames.h"
#include "RenderLayer.h"
#include "RenderView.h"

namespace WebCore {

using namespace HTMLNames;

RenderMarquee::RenderMarquee(RenderLayer* l)
    : m_layer(l), m_currentLoop(0)
    , m_totalLoops(0)
    , m_timer(*this, &RenderMarquee::timerFired)
    , m_start(0), m_end(0), m_speed(0), m_reset(false)
    , m_suspended(false), m_stopped(false), m_direction(MAUTO)
{
    l->setConstrainsScrollingToContentEdge(false);
}

RenderMarquee::~RenderMarquee()
{
}

int RenderMarquee::marqueeSpeed() const
{
    int result = m_layer->renderer().style().marqueeSpeed();
    Element* element = m_layer->renderer().element();
    if (is<HTMLMarqueeElement>(element))
        result = std::max(result, downcast<HTMLMarqueeElement>(*element).minimumDelay());
    return result;
}

EMarqueeDirection RenderMarquee::direction() const
{
    // FIXME: Support the CSS3 "auto" value for determining the direction of the marquee.
    // For now just map MAUTO to MBACKWARD
    EMarqueeDirection result = m_layer->renderer().style().marqueeDirection();
    TextDirection dir = m_layer->renderer().style().direction();
    if (result == MAUTO)
        result = MBACKWARD;
    if (result == MFORWARD)
        result = (dir == LTR) ? MRIGHT : MLEFT;
    if (result == MBACKWARD)
        result = (dir == LTR) ? MLEFT : MRIGHT;
    
    // Now we have the real direction.  Next we check to see if the increment is negative.
    // If so, then we reverse the direction.
    Length increment = m_layer->renderer().style().marqueeIncrement();
    if (increment.isNegative())
        result = static_cast<EMarqueeDirection>(-result);
    
    return result;
}

bool RenderMarquee::isHorizontal() const
{
    return direction() == MLEFT || direction() == MRIGHT;
}

int RenderMarquee::computePosition(EMarqueeDirection dir, bool stopAtContentEdge)
{
    RenderBox* box = m_layer->renderBox();
    ASSERT(box);
    auto& boxStyle = box->style();
    if (isHorizontal()) {
        bool ltr = boxStyle.isLeftToRightDirection();
        LayoutUnit clientWidth = box->clientWidth();
        LayoutUnit contentWidth = ltr ? box->maxPreferredLogicalWidth() : box->minPreferredLogicalWidth();
        if (ltr)
            contentWidth += (box->paddingRight() - box->borderLeft());
        else {
            contentWidth = box->width() - contentWidth;
            contentWidth += (box->paddingLeft() - box->borderRight());
        }
        if (dir == MRIGHT) {
            if (stopAtContentEdge)
                return std::max<LayoutUnit>(0, ltr ? (contentWidth - clientWidth) : (clientWidth - contentWidth));
            else
                return ltr ? contentWidth : clientWidth;
        }
        else {
            if (stopAtContentEdge)
                return std::min<LayoutUnit>(0, ltr ? (contentWidth - clientWidth) : (clientWidth - contentWidth));
            else
                return ltr ? -clientWidth : -contentWidth;
        }
    }
    else {
        int contentHeight = box->layoutOverflowRect().maxY() - box->borderTop() + box->paddingBottom();
        int clientHeight = roundToInt(box->clientHeight());
        if (dir == MUP) {
            if (stopAtContentEdge)
                 return std::min(contentHeight - clientHeight, 0);
            else
                return -clientHeight;
        }
        else {
            if (stopAtContentEdge)
                return std::max(contentHeight - clientHeight, 0);
            else 
                return contentHeight;
        }
    }    
}

void RenderMarquee::start()
{
    if (m_timer.isActive() || m_layer->renderer().style().marqueeIncrement().isZero())
        return;

    if (!m_suspended && !m_stopped) {
        if (isHorizontal())
            m_layer->scrollToOffset(ScrollOffset(m_start, 0));
        else
            m_layer->scrollToOffset(ScrollOffset(0, m_start));
    }
    else {
        m_suspended = false;
        m_stopped = false;
    }

    m_timer.startRepeating(1_ms * speed());
}

void RenderMarquee::suspend()
{
    m_timer.stop();
    m_suspended = true;
}

void RenderMarquee::stop()
{
    m_timer.stop();
    m_stopped = true;
}

void RenderMarquee::updateMarqueePosition()
{
    bool activate = (m_totalLoops <= 0 || m_currentLoop < m_totalLoops);
    if (activate) {
        EMarqueeBehavior behavior = m_layer->renderer().style().marqueeBehavior();
        m_start = computePosition(direction(), behavior == MALTERNATE);
        m_end = computePosition(reverseDirection(), behavior == MALTERNATE || behavior == MSLIDE);
        if (!m_stopped)
            start();
    }
}

void RenderMarquee::updateMarqueeStyle()
{
    auto& style = m_layer->renderer().mutableStyle();
    
    if (m_direction != style.marqueeDirection() || (m_totalLoops != style.marqueeLoopCount() && m_currentLoop >= m_totalLoops))
        m_currentLoop = 0; // When direction changes or our loopCount is a smaller number than our current loop, reset our loop.
    
    m_totalLoops = style.marqueeLoopCount();
    m_direction = style.marqueeDirection();
    
    if (m_layer->renderer().isHTMLMarquee()) {
        // Hack for WinIE.  In WinIE, a value of 0 or lower for the loop count for SLIDE means to only do
        // one loop.
        if (m_totalLoops <= 0 && style.marqueeBehavior() == MSLIDE)
            m_totalLoops = 1;
        
        // Hack alert: Set the white-space value to nowrap for horizontal marquees with inline children, thus ensuring
        // all the text ends up on one line by default.  Limit this hack to the <marquee> element to emulate
        // WinIE's behavior.  Someone using CSS3 can use white-space: nowrap on their own to get this effect.
        // Second hack alert: Set the text-align back to auto.  WinIE completely ignores text-align on the
        // marquee element.
        // FIXME: Bring these up with the CSS WG.
        if (isHorizontal() && m_layer->renderer().childrenInline()) {
            style.setWhiteSpace(NOWRAP);
            style.setTextAlign(TASTART);
        }
    }
    
    // Legacy hack - multiple browsers default vertical marquees to 200px tall.
    if (!isHorizontal() && style.height().isAuto())
        style.setHeight(Length(200, Fixed)); 
   
    if (speed() != marqueeSpeed()) {
        m_speed = marqueeSpeed();
        if (m_timer.isActive())
            m_timer.startRepeating(1_ms * speed());
    }
    
    // Check the loop count to see if we should now stop.
    bool activate = (m_totalLoops <= 0 || m_currentLoop < m_totalLoops);
    if (activate && !m_timer.isActive())
        m_layer->renderer().setNeedsLayout();
    else if (!activate && m_timer.isActive())
        m_timer.stop();
}

void RenderMarquee::timerFired()
{
    if (m_layer->renderer().view().needsLayout())
        return;
    
    if (m_reset) {
        m_reset = false;
        if (isHorizontal())
            m_layer->scrollToXOffset(m_start);
        else
            m_layer->scrollToYOffset(m_start);
        return;
    }
    
    const RenderStyle& style = m_layer->renderer().style();
    
    int endPoint = m_end;
    int range = m_end - m_start;
    int newPos;
    if (range == 0)
        newPos = m_end;
    else {  
        bool addIncrement = direction() == MUP || direction() == MLEFT;
        bool isReversed = style.marqueeBehavior() == MALTERNATE && m_currentLoop % 2;
        if (isReversed) {
            // We're going in the reverse direction.
            endPoint = m_start;
            range = -range;
            addIncrement = !addIncrement;
        }
        bool positive = range > 0;
        int clientSize = (isHorizontal() ? roundToInt(m_layer->renderBox()->clientWidth()) : roundToInt(m_layer->renderBox()->clientHeight()));
        int increment = abs(intValueForLength(m_layer->renderer().style().marqueeIncrement(), clientSize));
        int currentPos = (isHorizontal() ? m_layer->scrollOffset().x() : m_layer->scrollOffset().y());
        newPos =  currentPos + (addIncrement ? increment : -increment);
        if (positive)
            newPos = std::min(newPos, endPoint);
        else
            newPos = std::max(newPos, endPoint);
    }

    if (newPos == endPoint) {
        m_currentLoop++;
        if (m_totalLoops > 0 && m_currentLoop >= m_totalLoops)
            m_timer.stop();
        else if (style.marqueeBehavior() != MALTERNATE)
            m_reset = true;
    }
    
    if (isHorizontal())
        m_layer->scrollToXOffset(newPos);
    else
        m_layer->scrollToYOffset(newPos);
}

} // namespace WebCore
