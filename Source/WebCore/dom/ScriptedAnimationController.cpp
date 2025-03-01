/*
 * Copyright (C) 2011 Google Inc. All Rights Reserved.
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
 *  THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS'' AND ANY
 *  EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 *  WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 *  DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS BE LIABLE FOR ANY
 *  DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 *  (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 *  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 *  ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 *  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 *  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include "config.h"
#include "ScriptedAnimationController.h"

#include "DOMWindow.h"
#include "DisplayRefreshMonitor.h"
#include "DisplayRefreshMonitorManager.h"
#include "Document.h"
#include "DocumentLoader.h"
#include "FrameView.h"
#include "InspectorInstrumentation.h"
#include "Logging.h"
#include "MainFrame.h"
#include "Page.h"
#include "RequestAnimationFrameCallback.h"
#include "Settings.h"
#include <wtf/Ref.h>
#include <wtf/SystemTracing.h>
#include <wtf/text/StringBuilder.h>

#if USE(REQUEST_ANIMATION_FRAME_TIMER)
#include <algorithm>
#include <wtf/CurrentTime.h>

// Allow a little more than 60fps to make sure we can at least hit that frame rate.
static const Seconds fullSpeedAnimationInterval { 15_ms };
// Allow a little more than 30fps to make sure we can at least hit that frame rate.
static const Seconds halfSpeedThrottlingAnimationInterval { 30_ms };
static const Seconds aggressiveThrottlingAnimationInterval { 10_s };
#endif

#define RELEASE_LOG_IF_ALLOWED(fmt, ...) RELEASE_LOG_IF(page() && page()->isAlwaysOnLoggingAllowed(), PerformanceLogging, "%p - ScriptedAnimationController::" fmt, this, ##__VA_ARGS__)

namespace WebCore {

ScriptedAnimationController::ScriptedAnimationController(Document& document, PlatformDisplayID displayID)
    : m_document(&document)
#if USE(REQUEST_ANIMATION_FRAME_TIMER)
    , m_animationTimer(*this, &ScriptedAnimationController::animationTimerFired)
#endif
{
    windowScreenDidChange(displayID);
}

ScriptedAnimationController::~ScriptedAnimationController()
{
}

bool ScriptedAnimationController::requestAnimationFrameEnabled() const
{
    return m_document && m_document->settings().requestAnimationFrameEnabled();
}

void ScriptedAnimationController::suspend()
{
    dispatchLoggingEventIfRequired("raf-suspend");
    if (m_document && m_document->frame() && m_document->frame()->settings().shouldDispatchRequestAnimationFrameEvents())
        WTFReportBacktrace();
    ++m_suspendCount;
}

void ScriptedAnimationController::resume()
{
    dispatchLoggingEventIfRequired("raf-resume");
    // It would be nice to put an ASSERT(m_suspendCount > 0) here, but in WK1 resume() can be called
    // even when suspend hasn't (if a tab was created in the background).
    if (m_suspendCount > 0)
        --m_suspendCount;

    if (!m_suspendCount && m_callbacks.size())
        scheduleAnimation();
}

#if USE(REQUEST_ANIMATION_FRAME_TIMER) && USE(REQUEST_ANIMATION_FRAME_DISPLAY_MONITOR) && !RELEASE_LOG_DISABLED

static const char* throttlingReasonToString(ScriptedAnimationController::ThrottlingReason reason)
{
    switch (reason) {
    case ScriptedAnimationController::ThrottlingReason::VisuallyIdle:
        return "VisuallyIdle";
    case ScriptedAnimationController::ThrottlingReason::OutsideViewport:
        return "OutsideViewport";
    case ScriptedAnimationController::ThrottlingReason::LowPowerMode:
        return "LowPowerMode";
    case ScriptedAnimationController::ThrottlingReason::NonInteractedCrossOriginFrame:
        return "NonInteractiveCrossOriginFrame";
    }
}

static String throttlingReasonsToString(OptionSet<ScriptedAnimationController::ThrottlingReason> reasons)
{
    if (reasons.isEmpty())
        return ASCIILiteral("[Unthrottled]");

    StringBuilder builder;
    for (auto reason : reasons) {
        if (!builder.isEmpty())
            builder.append('|');
        builder.append(throttlingReasonToString(reason));
    }
    return builder.toString();
}

#endif

void ScriptedAnimationController::addThrottlingReason(ThrottlingReason reason)
{
#if USE(REQUEST_ANIMATION_FRAME_TIMER) && USE(REQUEST_ANIMATION_FRAME_DISPLAY_MONITOR)
    if (m_throttlingReasons.contains(reason))
        return;

    m_throttlingReasons |= reason;

    RELEASE_LOG_IF_ALLOWED("addThrottlingReason(%s) -> %s", throttlingReasonToString(reason), throttlingReasonsToString(m_throttlingReasons).utf8().data());
    dispatchLoggingEventIfRequired("raf-add-throttling-reason");

    if (m_animationTimer.isActive()) {
        m_animationTimer.stop();
        scheduleAnimation();
    }
#else
    UNUSED_PARAM(reason);
#endif
}

void ScriptedAnimationController::removeThrottlingReason(ThrottlingReason reason)
{
#if USE(REQUEST_ANIMATION_FRAME_TIMER) && USE(REQUEST_ANIMATION_FRAME_DISPLAY_MONITOR)
    if (!m_throttlingReasons.contains(reason))
        return;

    m_throttlingReasons -= reason;

    RELEASE_LOG_IF_ALLOWED("removeThrottlingReason(%s) -> %s", throttlingReasonToString(reason), throttlingReasonsToString(m_throttlingReasons).utf8().data());
    dispatchLoggingEventIfRequired("raf-remove-throttling-reason");

    if (m_animationTimer.isActive()) {
        m_animationTimer.stop();
        scheduleAnimation();
    }
#else
    UNUSED_PARAM(reason);
#endif
}

bool ScriptedAnimationController::isThrottled() const
{
#if USE(REQUEST_ANIMATION_FRAME_TIMER) && USE(REQUEST_ANIMATION_FRAME_DISPLAY_MONITOR)
    return !m_throttlingReasons.isEmpty();
#else
    return false;
#endif
}

ScriptedAnimationController::CallbackId ScriptedAnimationController::registerCallback(Ref<RequestAnimationFrameCallback>&& callback)
{
    ScriptedAnimationController::CallbackId id = ++m_nextCallbackId;
    callback->m_firedOrCancelled = false;
    callback->m_id = id;
    m_callbacks.append(WTFMove(callback));

    InspectorInstrumentation::didRequestAnimationFrame(m_document, id);
    dispatchLoggingEventIfRequired("raf-register-callback");

    if (!m_suspendCount)
        scheduleAnimation();
    return id;
}

void ScriptedAnimationController::cancelCallback(CallbackId id)
{
    dispatchLoggingEventIfRequired("raf-cancel-callback");
    for (size_t i = 0; i < m_callbacks.size(); ++i) {
        if (m_callbacks[i]->m_id == id) {
            m_callbacks[i]->m_firedOrCancelled = true;
            InspectorInstrumentation::didCancelAnimationFrame(m_document, id);
            m_callbacks.remove(i);
            dispatchLoggingEventIfRequired("raf-did-cancel-callback");
            return;
        }
    }
}

void ScriptedAnimationController::serviceScriptedAnimations(double timestamp)
{
    dispatchLoggingEventIfRequired("raf-service-scripted-animations");
    if (!m_callbacks.size() || m_suspendCount || !requestAnimationFrameEnabled())
        return;

    TraceScope tracingScope(RAFCallbackStart, RAFCallbackEnd);

    double highResNowMs = 1000 * timestamp;
    double legacyHighResNowMs = 1000 * (timestamp + m_document->loader()->timing().referenceWallTime().secondsSinceEpoch().seconds());

    // First, generate a list of callbacks to consider.  Callbacks registered from this point
    // on are considered only for the "next" frame, not this one.
    CallbackList callbacks(m_callbacks);

    // Invoking callbacks may detach elements from our document, which clears the document's
    // reference to us, so take a defensive reference.
    Ref<ScriptedAnimationController> protectedThis(*this);

    for (auto& callback : callbacks) {
        if (!callback->m_firedOrCancelled) {
            callback->m_firedOrCancelled = true;
            InspectorInstrumentationCookie cookie = InspectorInstrumentation::willFireAnimationFrame(m_document, callback->m_id);
            dispatchLoggingEventIfRequired("raf-will-fire");
            if (callback->m_useLegacyTimeBase)
                callback->handleEvent(legacyHighResNowMs);
            else
                callback->handleEvent(highResNowMs);
            InspectorInstrumentation::didFireAnimationFrame(cookie);
            dispatchLoggingEventIfRequired("raf-did-fire");
        }
    }

    // Remove any callbacks we fired from the list of pending callbacks.
    for (size_t i = 0; i < m_callbacks.size();) {
        if (m_callbacks[i]->m_firedOrCancelled)
            m_callbacks.remove(i);
        else
            ++i;
    }

    if (m_callbacks.size())
        scheduleAnimation();
}

void ScriptedAnimationController::windowScreenDidChange(PlatformDisplayID displayID)
{
    if (!requestAnimationFrameEnabled())
        return;
#if USE(REQUEST_ANIMATION_FRAME_DISPLAY_MONITOR)
    DisplayRefreshMonitorManager::sharedManager().windowScreenDidChange(displayID, *this);
#else
    UNUSED_PARAM(displayID);
#endif
}

Seconds ScriptedAnimationController::interval() const
{
#if USE(REQUEST_ANIMATION_FRAME_TIMER) && USE(REQUEST_ANIMATION_FRAME_DISPLAY_MONITOR)
    if (m_throttlingReasons.contains(ThrottlingReason::VisuallyIdle) || m_throttlingReasons.contains(ThrottlingReason::OutsideViewport))
        return aggressiveThrottlingAnimationInterval;

    if (m_throttlingReasons.contains(ThrottlingReason::LowPowerMode))
        return halfSpeedThrottlingAnimationInterval;

    if (m_throttlingReasons.contains(ThrottlingReason::NonInteractedCrossOriginFrame))
        return halfSpeedThrottlingAnimationInterval;

    ASSERT(m_throttlingReasons.isEmpty());
#endif
    return fullSpeedAnimationInterval;
}

Page* ScriptedAnimationController::page() const
{
    return m_document ? m_document->page() : nullptr;
}

void ScriptedAnimationController::scheduleAnimation()
{
    dispatchLoggingEventIfRequired("raf-schedule-animation");
    if (!requestAnimationFrameEnabled())
        return;

#if USE(REQUEST_ANIMATION_FRAME_TIMER)
#if USE(REQUEST_ANIMATION_FRAME_DISPLAY_MONITOR)
    if (!m_isUsingTimer && !isThrottled()) {
        if (DisplayRefreshMonitorManager::sharedManager().scheduleAnimation(*this))
            return;

        m_isUsingTimer = true;
        dispatchLoggingEventIfRequired("raf-schedule-animation-display-refresh-monitor");
    }
#endif
    if (m_animationTimer.isActive())
        return;

    Seconds animationInterval = interval();
    Seconds scheduleDelay = std::max(animationInterval - Seconds(m_document->domWindow()->nowTimestamp() - m_lastAnimationFrameTimestamp), 0_s);

    if (isThrottled()) {
        // FIXME: not ideal to snapshot time both in now() and nowTimestamp(), the latter of which also has reduced resolution.
        MonotonicTime now = MonotonicTime::now();

        MonotonicTime fireTime = now + scheduleDelay;
        Seconds alignmentInterval = 10_ms;
        // Snap to the nearest alignmentInterval.
        Seconds alignment = (fireTime + alignmentInterval / 2) % alignmentInterval;
        MonotonicTime alignedFireTime = fireTime - alignment;
        scheduleDelay = alignedFireTime - now;
    }

    m_animationTimer.startOneShot(scheduleDelay);

    dispatchLoggingEventIfRequired("raf-schedule-animation-timer");
#else
    if (FrameView* frameView = m_document->view())
        frameView->scheduleAnimation();
    dispatchLoggingEventIfRequired("raf-schedule-animation-frame-view");
#endif
}

#if USE(REQUEST_ANIMATION_FRAME_TIMER)
void ScriptedAnimationController::animationTimerFired()
{
    m_lastAnimationFrameTimestamp = m_document->domWindow()->nowTimestamp();
    serviceScriptedAnimations(m_lastAnimationFrameTimestamp);
}
#if USE(REQUEST_ANIMATION_FRAME_DISPLAY_MONITOR)
void ScriptedAnimationController::displayRefreshFired()
{
    serviceScriptedAnimations(m_document->domWindow()->nowTimestamp());
}
#endif
#endif


#if USE(REQUEST_ANIMATION_FRAME_DISPLAY_MONITOR)
RefPtr<DisplayRefreshMonitor> ScriptedAnimationController::createDisplayRefreshMonitor(PlatformDisplayID displayID) const
{
    if (!m_document->page())
        return nullptr;

    if (auto monitor = m_document->page()->chrome().client().createDisplayRefreshMonitor(displayID))
        return monitor;

    return DisplayRefreshMonitor::createDefaultDisplayRefreshMonitor(displayID);
}
#endif

void ScriptedAnimationController::dispatchLoggingEventIfRequired(const AtomicString& type)
{
    if (m_document && m_document->frame() && m_document->frame()->settings().shouldDispatchRequestAnimationFrameEvents())
        m_document->dispatchEvent(Event::create(type, false, false));
}

}
