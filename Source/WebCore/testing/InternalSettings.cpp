/*
 * Copyright (C) 2012 Google Inc. All rights reserved.
 * Copyright (C) 2013 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1.  Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 * 2.  Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE AND ITS CONTRIBUTORS "AS IS" AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL APPLE OR ITS CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include "InternalSettings.h"

#include "CaptionUserPreferences.h"
#include "Document.h"
#include "ExceptionCode.h"
#include "FrameView.h"
#include "Language.h"
#include "LocaleToScriptMapping.h"
#include "MainFrame.h"
#include "Page.h"
#include "PageGroup.h"
#include "RuntimeEnabledFeatures.h"
#include "Settings.h"
#include "Supplementable.h"
#include "TextRun.h"

#if ENABLE(INPUT_TYPE_COLOR)
#include "ColorChooser.h"
#endif

#if USE(SOUP)
#include "SoupNetworkSession.h"
#endif

namespace WebCore {

InternalSettings::Backup::Backup(Settings& settings)
    : m_originalEditingBehavior(settings.editingBehaviorType())
#if ENABLE(TEXT_AUTOSIZING)
    , m_originalTextAutosizingEnabled(settings.textAutosizingEnabled())
    , m_originalTextAutosizingWindowSizeOverride(settings.textAutosizingWindowSizeOverride())
#endif
    , m_originalMediaTypeOverride(settings.mediaTypeOverride())
    , m_originalCanvasUsesAcceleratedDrawing(settings.canvasUsesAcceleratedDrawing())
    , m_originalMockScrollbarsEnabled(settings.mockScrollbarsEnabled())
    , m_imagesEnabled(settings.areImagesEnabled())
    , m_preferMIMETypeForImages(settings.preferMIMETypeForImages())
    , m_minimumDOMTimerInterval(settings.minimumDOMTimerInterval())
#if ENABLE(VIDEO_TRACK)
    , m_shouldDisplaySubtitles(settings.shouldDisplaySubtitles())
    , m_shouldDisplayCaptions(settings.shouldDisplayCaptions())
    , m_shouldDisplayTextDescriptions(settings.shouldDisplayTextDescriptions())
#endif
    , m_defaultVideoPosterURL(settings.defaultVideoPosterURL())
    , m_forcePendingWebGLPolicy(settings.isForcePendingWebGLPolicy())
    , m_originalTimeWithoutMouseMovementBeforeHidingControls(settings.timeWithoutMouseMovementBeforeHidingControls())
    , m_useLegacyBackgroundSizeShorthandBehavior(settings.useLegacyBackgroundSizeShorthandBehavior())
    , m_autoscrollForDragAndDropEnabled(settings.autoscrollForDragAndDropEnabled())
    , m_quickTimePluginReplacementEnabled(settings.quickTimePluginReplacementEnabled())
    , m_youTubeFlashPluginReplacementEnabled(settings.youTubeFlashPluginReplacementEnabled())
    , m_shouldConvertPositionStyleOnCopy(settings.shouldConvertPositionStyleOnCopy())
    , m_fontFallbackPrefersPictographs(settings.fontFallbackPrefersPictographs())
    , m_webFontsAlwaysFallBack(settings.webFontsAlwaysFallBack())
    , m_backgroundShouldExtendBeyondPage(settings.backgroundShouldExtendBeyondPage())
    , m_storageBlockingPolicy(settings.storageBlockingPolicy())
    , m_scrollingTreeIncludesFrames(settings.scrollingTreeIncludesFrames())
#if ENABLE(TOUCH_EVENTS)
    , m_touchEventEmulationEnabled(settings.isTouchEventEmulationEnabled())
#endif
#if ENABLE(WIRELESS_PLAYBACK_TARGET)
    , m_allowsAirPlayForMediaPlayback(settings.allowsAirPlayForMediaPlayback())
#endif
    , m_allowsInlineMediaPlayback(settings.allowsInlineMediaPlayback())
    , m_allowsInlineMediaPlaybackAfterFullscreen(settings.allowsInlineMediaPlaybackAfterFullscreen())
    , m_inlineMediaPlaybackRequiresPlaysInlineAttribute(settings.inlineMediaPlaybackRequiresPlaysInlineAttribute())
    , m_deferredCSSParserEnabled(settings.deferredCSSParserEnabled())
    , m_inputEventsEnabled(settings.inputEventsEnabled())
    , m_userInterfaceDirectionPolicy(settings.userInterfaceDirectionPolicy())
    , m_systemLayoutDirection(settings.systemLayoutDirection())
    , m_pdfImageCachingPolicy(settings.pdfImageCachingPolicy())
    , m_forcedColorsAreInvertedAccessibilityValue(settings.forcedColorsAreInvertedAccessibilityValue())
    , m_forcedDisplayIsMonochromeAccessibilityValue(settings.forcedDisplayIsMonochromeAccessibilityValue())
    , m_forcedPrefersReducedMotionAccessibilityValue(settings.forcedPrefersReducedMotionAccessibilityValue())
#if ENABLE(INDEXED_DATABASE_IN_WORKERS)
    , m_indexedDBWorkersEnabled(RuntimeEnabledFeatures::sharedFeatures().indexedDBWorkersEnabled())
#endif
    , m_cssGridLayoutEnabled(RuntimeEnabledFeatures::sharedFeatures().isCSSGridLayoutEnabled())
#if ENABLE(WEBGL2)
    , m_webGL2Enabled(RuntimeEnabledFeatures::sharedFeatures().webGL2Enabled())
#endif
#if ENABLE(WEBGPU)
    , m_webGPUEnabled(RuntimeEnabledFeatures::sharedFeatures().webGPUEnabled())
#endif
{
}

void InternalSettings::Backup::restoreTo(Settings& settings)
{
    settings.setEditingBehaviorType(m_originalEditingBehavior);

    for (const auto& standardFont : m_standardFontFamilies)
        settings.setStandardFontFamily(standardFont.value, static_cast<UScriptCode>(standardFont.key));
    m_standardFontFamilies.clear();

    for (const auto& fixedFont : m_fixedFontFamilies)
        settings.setFixedFontFamily(fixedFont.value, static_cast<UScriptCode>(fixedFont.key));
    m_fixedFontFamilies.clear();

    for (const auto& serifFont : m_serifFontFamilies)
        settings.setSerifFontFamily(serifFont.value, static_cast<UScriptCode>(serifFont.key));
    m_serifFontFamilies.clear();

    for (const auto& sansSerifFont : m_sansSerifFontFamilies)
        settings.setSansSerifFontFamily(sansSerifFont.value, static_cast<UScriptCode>(sansSerifFont.key));
    m_sansSerifFontFamilies.clear();

    for (const auto& cursiveFont : m_cursiveFontFamilies)
        settings.setCursiveFontFamily(cursiveFont.value, static_cast<UScriptCode>(cursiveFont.key));
    m_cursiveFontFamilies.clear();

    for (const auto& fantasyFont : m_fantasyFontFamilies)
        settings.setFantasyFontFamily(fantasyFont.value, static_cast<UScriptCode>(fantasyFont.key));
    m_fantasyFontFamilies.clear();

    for (const auto& pictographFont : m_pictographFontFamilies)
        settings.setPictographFontFamily(pictographFont.value, static_cast<UScriptCode>(pictographFont.key));
    m_pictographFontFamilies.clear();

#if ENABLE(TEXT_AUTOSIZING)
    settings.setTextAutosizingEnabled(m_originalTextAutosizingEnabled);
    settings.setTextAutosizingWindowSizeOverride(m_originalTextAutosizingWindowSizeOverride);
#endif
    settings.setMediaTypeOverride(m_originalMediaTypeOverride);
    settings.setCanvasUsesAcceleratedDrawing(m_originalCanvasUsesAcceleratedDrawing);
    settings.setImagesEnabled(m_imagesEnabled);
    settings.setPreferMIMETypeForImages(m_preferMIMETypeForImages);
    settings.setMinimumDOMTimerInterval(m_minimumDOMTimerInterval);
#if ENABLE(VIDEO_TRACK)
    settings.setShouldDisplaySubtitles(m_shouldDisplaySubtitles);
    settings.setShouldDisplayCaptions(m_shouldDisplayCaptions);
    settings.setShouldDisplayTextDescriptions(m_shouldDisplayTextDescriptions);
#endif
    settings.setDefaultVideoPosterURL(m_defaultVideoPosterURL);
    settings.setForcePendingWebGLPolicy(m_forcePendingWebGLPolicy);
    settings.setTimeWithoutMouseMovementBeforeHidingControls(m_originalTimeWithoutMouseMovementBeforeHidingControls);
    settings.setUseLegacyBackgroundSizeShorthandBehavior(m_useLegacyBackgroundSizeShorthandBehavior);
    settings.setAutoscrollForDragAndDropEnabled(m_autoscrollForDragAndDropEnabled);
    settings.setShouldConvertPositionStyleOnCopy(m_shouldConvertPositionStyleOnCopy);
    settings.setFontFallbackPrefersPictographs(m_fontFallbackPrefersPictographs);
    settings.setWebFontsAlwaysFallBack(m_webFontsAlwaysFallBack);
    settings.setBackgroundShouldExtendBeyondPage(m_backgroundShouldExtendBeyondPage);
    settings.setStorageBlockingPolicy(m_storageBlockingPolicy);
    settings.setScrollingTreeIncludesFrames(m_scrollingTreeIncludesFrames);
#if ENABLE(TOUCH_EVENTS)
    settings.setTouchEventEmulationEnabled(m_touchEventEmulationEnabled);
#endif
    settings.setAllowsInlineMediaPlayback(m_allowsInlineMediaPlayback);
    settings.setAllowsInlineMediaPlaybackAfterFullscreen(m_allowsInlineMediaPlaybackAfterFullscreen);
    settings.setInlineMediaPlaybackRequiresPlaysInlineAttribute(m_inlineMediaPlaybackRequiresPlaysInlineAttribute);
    settings.setQuickTimePluginReplacementEnabled(m_quickTimePluginReplacementEnabled);
    settings.setYouTubeFlashPluginReplacementEnabled(m_youTubeFlashPluginReplacementEnabled);
    settings.setDeferredCSSParserEnabled(m_deferredCSSParserEnabled);
    settings.setInputEventsEnabled(m_inputEventsEnabled);
    settings.setUserInterfaceDirectionPolicy(m_userInterfaceDirectionPolicy);
    settings.setSystemLayoutDirection(m_systemLayoutDirection);
    settings.setPdfImageCachingPolicy(m_pdfImageCachingPolicy);
    settings.setForcedColorsAreInvertedAccessibilityValue(m_forcedColorsAreInvertedAccessibilityValue);
    settings.setForcedDisplayIsMonochromeAccessibilityValue(m_forcedDisplayIsMonochromeAccessibilityValue);
    settings.setForcedPrefersReducedMotionAccessibilityValue(m_forcedPrefersReducedMotionAccessibilityValue);
    Settings::setAllowsAnySSLCertificate(false);

#if ENABLE(INDEXED_DATABASE_IN_WORKERS)
    RuntimeEnabledFeatures::sharedFeatures().setIndexedDBWorkersEnabled(m_indexedDBWorkersEnabled);
#endif
    RuntimeEnabledFeatures::sharedFeatures().setCSSGridLayoutEnabled(m_cssGridLayoutEnabled);
#if ENABLE(WEBGL2)
    RuntimeEnabledFeatures::sharedFeatures().setWebGL2Enabled(m_webGL2Enabled);
#endif
#if ENABLE(WEBGPU)
    RuntimeEnabledFeatures::sharedFeatures().setWebGPUEnabled(m_webGPUEnabled);
#endif
}

class InternalSettingsWrapper : public Supplement<Page> {
public:
    explicit InternalSettingsWrapper(Page* page)
        : m_internalSettings(InternalSettings::create(page)) { }
    virtual ~InternalSettingsWrapper() { m_internalSettings->hostDestroyed(); }
#if !ASSERT_DISABLED
    bool isRefCountedWrapper() const override { return true; }
#endif
    InternalSettings* internalSettings() const { return m_internalSettings.get(); }

private:
    RefPtr<InternalSettings> m_internalSettings;
};

const char* InternalSettings::supplementName()
{
    return "InternalSettings";
}

InternalSettings* InternalSettings::from(Page* page)
{
    if (!Supplement<Page>::from(page, supplementName()))
        Supplement<Page>::provideTo(page, supplementName(), std::make_unique<InternalSettingsWrapper>(page));
    return static_cast<InternalSettingsWrapper*>(Supplement<Page>::from(page, supplementName()))->internalSettings();
}

void InternalSettings::hostDestroyed()
{
    m_page = nullptr;
}

InternalSettings::InternalSettings(Page* page)
    : InternalSettingsGenerated(page)
    , m_page(page)
    , m_backup(page->settings())
{
#if ENABLE(WIRELESS_PLAYBACK_TARGET)
    setAllowsAirPlayForMediaPlayback(false);
#endif
#if ENABLE(MEDIA_STREAM)
    setMediaCaptureRequiresSecureConnection(false);
#endif
}

Ref<InternalSettings> InternalSettings::create(Page* page)
{
    return adoptRef(*new InternalSettings(page));
}

void InternalSettings::resetToConsistentState()
{
    m_page->setPageScaleFactor(1, { 0, 0 });
    m_page->mainFrame().setPageAndTextZoomFactors(1, 1);
    m_page->setCanStartMedia(true);

    settings().setShouldDispatchRequestAnimationFrameEvents(false);

    settings().setForcePendingWebGLPolicy(false);
#if ENABLE(WIRELESS_PLAYBACK_TARGET)
    settings().setAllowsAirPlayForMediaPlayback(false);
#endif
#if ENABLE(MEDIA_STREAM)
    setMediaCaptureRequiresSecureConnection(false);
#endif

    m_backup.restoreTo(settings());
    m_backup = Backup { settings() };

    InternalSettingsGenerated::resetToConsistentState();
}

Settings& InternalSettings::settings() const
{
    ASSERT(m_page);
    return m_page->settings();
}

ExceptionOr<void> InternalSettings::setTouchEventEmulationEnabled(bool enabled)
{
    if (!m_page)
        return Exception { INVALID_ACCESS_ERR };
#if ENABLE(TOUCH_EVENTS)
    settings().setTouchEventEmulationEnabled(enabled);
#else
    UNUSED_PARAM(enabled);
#endif
    return { };
}

ExceptionOr<void> InternalSettings::setStandardFontFamily(const String& family, const String& script)
{
    if (!m_page)
        return Exception { INVALID_ACCESS_ERR };
    UScriptCode code = scriptNameToCode(script);
    if (code == USCRIPT_INVALID_CODE)
        return { };
    m_backup.m_standardFontFamilies.add(code, settings().standardFontFamily(code));
    settings().setStandardFontFamily(family, code);
    return { };
}

ExceptionOr<void> InternalSettings::setSerifFontFamily(const String& family, const String& script)
{
    if (!m_page)
        return Exception { INVALID_ACCESS_ERR };
    UScriptCode code = scriptNameToCode(script);
    if (code == USCRIPT_INVALID_CODE)
        return { };
    m_backup.m_serifFontFamilies.add(code, settings().serifFontFamily(code));
    settings().setSerifFontFamily(family, code);
    return { };
}

ExceptionOr<void> InternalSettings::setSansSerifFontFamily(const String& family, const String& script)
{
    if (!m_page)
        return Exception { INVALID_ACCESS_ERR };
    UScriptCode code = scriptNameToCode(script);
    if (code == USCRIPT_INVALID_CODE)
        return { };
    m_backup.m_sansSerifFontFamilies.add(code, settings().sansSerifFontFamily(code));
    settings().setSansSerifFontFamily(family, code);
    return { };
}

ExceptionOr<void> InternalSettings::setFixedFontFamily(const String& family, const String& script)
{
    if (!m_page)
        return Exception { INVALID_ACCESS_ERR };
    UScriptCode code = scriptNameToCode(script);
    if (code == USCRIPT_INVALID_CODE)
        return { };
    m_backup.m_fixedFontFamilies.add(code, settings().fixedFontFamily(code));
    settings().setFixedFontFamily(family, code);
    return { };
}

ExceptionOr<void> InternalSettings::setCursiveFontFamily(const String& family, const String& script)
{
    if (!m_page)
        return Exception { INVALID_ACCESS_ERR };
    UScriptCode code = scriptNameToCode(script);
    if (code == USCRIPT_INVALID_CODE)
        return { };
    m_backup.m_cursiveFontFamilies.add(code, settings().cursiveFontFamily(code));
    settings().setCursiveFontFamily(family, code);
    return { };
}

ExceptionOr<void> InternalSettings::setFantasyFontFamily(const String& family, const String& script)
{
    if (!m_page)
        return Exception { INVALID_ACCESS_ERR };
    UScriptCode code = scriptNameToCode(script);
    if (code == USCRIPT_INVALID_CODE)
        return { };
    m_backup.m_fantasyFontFamilies.add(code, settings().fantasyFontFamily(code));
    settings().setFantasyFontFamily(family, code);
    return { };
}

ExceptionOr<void> InternalSettings::setPictographFontFamily(const String& family, const String& script)
{
    if (!m_page)
        return Exception { INVALID_ACCESS_ERR };
    UScriptCode code = scriptNameToCode(script);
    if (code == USCRIPT_INVALID_CODE)
        return { };
    m_backup.m_pictographFontFamilies.add(code, settings().pictographFontFamily(code));
    settings().setPictographFontFamily(family, code);
    return { };
}

ExceptionOr<void> InternalSettings::setTextAutosizingEnabled(bool enabled)
{
    if (!m_page)
        return Exception { INVALID_ACCESS_ERR };
#if ENABLE(TEXT_AUTOSIZING)
    settings().setTextAutosizingEnabled(enabled);
#else
    UNUSED_PARAM(enabled);
#endif
    return { };
}

ExceptionOr<void> InternalSettings::setTextAutosizingWindowSizeOverride(int width, int height)
{
    if (!m_page)
        return Exception { INVALID_ACCESS_ERR };
#if ENABLE(TEXT_AUTOSIZING)
    settings().setTextAutosizingWindowSizeOverride(IntSize(width, height));
#else
    UNUSED_PARAM(width);
    UNUSED_PARAM(height);
#endif
    return { };
}

ExceptionOr<void> InternalSettings::setMediaTypeOverride(const String& mediaType)
{
    if (!m_page)
        return Exception { INVALID_ACCESS_ERR };
    settings().setMediaTypeOverride(mediaType);
    return { };
}

ExceptionOr<void> InternalSettings::setCanStartMedia(bool enabled)
{
    if (!m_page)
        return Exception { INVALID_ACCESS_ERR };
    m_page->setCanStartMedia(enabled);
    return { };
}

ExceptionOr<void> InternalSettings::setAllowsAirPlayForMediaPlayback(bool allows)
{
    if (!m_page)
        return Exception { INVALID_ACCESS_ERR };
#if ENABLE(WIRELESS_PLAYBACK_TARGET)
    settings().setAllowsAirPlayForMediaPlayback(allows);
#else
    UNUSED_PARAM(allows);
#endif
    return { };
}

ExceptionOr<void> InternalSettings::setMediaCaptureRequiresSecureConnection(bool requires)
{
    if (!m_page)
        return Exception { INVALID_ACCESS_ERR };
#if ENABLE(MEDIA_STREAM)
    settings().setMediaCaptureRequiresSecureConnection(requires);
#else
    UNUSED_PARAM(requires);
#endif
    return { };
}

ExceptionOr<void> InternalSettings::setEditingBehavior(const String& editingBehavior)
{
    if (!m_page)
        return Exception { INVALID_ACCESS_ERR };
    if (equalLettersIgnoringASCIICase(editingBehavior, "win"))
        settings().setEditingBehaviorType(EditingWindowsBehavior);
    else if (equalLettersIgnoringASCIICase(editingBehavior, "mac"))
        settings().setEditingBehaviorType(EditingMacBehavior);
    else if (equalLettersIgnoringASCIICase(editingBehavior, "unix"))
        settings().setEditingBehaviorType(EditingUnixBehavior);
    else if (equalLettersIgnoringASCIICase(editingBehavior, "ios"))
        settings().setEditingBehaviorType(EditingIOSBehavior);
    else
        return Exception { SYNTAX_ERR };
    return { };
}

ExceptionOr<void> InternalSettings::setShouldDisplayTrackKind(const String& kind, bool enabled)
{
    if (!m_page)
        return Exception { INVALID_ACCESS_ERR };
#if ENABLE(VIDEO_TRACK)
    auto& captionPreferences = m_page->group().captionPreferences();
    if (equalLettersIgnoringASCIICase(kind, "subtitles"))
        captionPreferences.setUserPrefersSubtitles(enabled);
    else if (equalLettersIgnoringASCIICase(kind, "captions"))
        captionPreferences.setUserPrefersCaptions(enabled);
    else if (equalLettersIgnoringASCIICase(kind, "textdescriptions"))
        captionPreferences.setUserPrefersTextDescriptions(enabled);
    else
        return Exception { SYNTAX_ERR };
#else
    UNUSED_PARAM(kind);
    UNUSED_PARAM(enabled);
#endif
    return { };
}

ExceptionOr<bool> InternalSettings::shouldDisplayTrackKind(const String& kind)
{
    if (!m_page)
        return Exception { INVALID_ACCESS_ERR };
#if ENABLE(VIDEO_TRACK)
    auto& captionPreferences = m_page->group().captionPreferences();
    if (equalLettersIgnoringASCIICase(kind, "subtitles"))
        return captionPreferences.userPrefersSubtitles();
    if (equalLettersIgnoringASCIICase(kind, "captions"))
        return captionPreferences.userPrefersCaptions();
    if (equalLettersIgnoringASCIICase(kind, "textdescriptions"))
        return captionPreferences.userPrefersTextDescriptions();

    return Exception { SYNTAX_ERR };
#else
    UNUSED_PARAM(kind);
    return false;
#endif
}

ExceptionOr<void> InternalSettings::setStorageBlockingPolicy(const String& mode)
{
    if (!m_page)
        return Exception { INVALID_ACCESS_ERR };
    if (mode == "AllowAll")
        settings().setStorageBlockingPolicy(SecurityOrigin::AllowAllStorage);
    else if (mode == "BlockThirdParty")
        settings().setStorageBlockingPolicy(SecurityOrigin::BlockThirdPartyStorage);
    else if (mode == "BlockAll")
        settings().setStorageBlockingPolicy(SecurityOrigin::BlockAllStorage);
    else
        return Exception { SYNTAX_ERR };
    return { };
}

ExceptionOr<void> InternalSettings::setPreferMIMETypeForImages(bool preferMIMETypeForImages)
{
    if (!m_page)
        return Exception { INVALID_ACCESS_ERR };
    settings().setPreferMIMETypeForImages(preferMIMETypeForImages);
    return { };
}

ExceptionOr<void> InternalSettings::setImagesEnabled(bool enabled)
{
    if (!m_page)
        return Exception { INVALID_ACCESS_ERR };
    settings().setImagesEnabled(enabled);
    return { };
}

ExceptionOr<void> InternalSettings::setPDFImageCachingPolicy(const String& policy)
{
    if (!m_page)
        return Exception { INVALID_ACCESS_ERR };
    if (equalLettersIgnoringASCIICase(policy, "disabled"))
        settings().setPdfImageCachingPolicy(PDFImageCachingDisabled);
    else if (equalLettersIgnoringASCIICase(policy, "belowmemorylimit"))
        settings().setPdfImageCachingPolicy(PDFImageCachingBelowMemoryLimit);
    else if (equalLettersIgnoringASCIICase(policy, "clipboundsonly"))
        settings().setPdfImageCachingPolicy(PDFImageCachingClipBoundsOnly);
    else if (equalLettersIgnoringASCIICase(policy, "enabled"))
        settings().setPdfImageCachingPolicy(PDFImageCachingEnabled);
    else
        return Exception { SYNTAX_ERR };
    return { };
}

ExceptionOr<void> InternalSettings::setMinimumTimerInterval(double intervalInSeconds)
{
    if (!m_page)
        return Exception { INVALID_ACCESS_ERR };
    settings().setMinimumDOMTimerInterval(Seconds { intervalInSeconds });
    return { };
}

ExceptionOr<void> InternalSettings::setDefaultVideoPosterURL(const String& url)
{
    if (!m_page)
        return Exception { INVALID_ACCESS_ERR };
    settings().setDefaultVideoPosterURL(url);
    return { };
}

ExceptionOr<void> InternalSettings::setForcePendingWebGLPolicy(bool forced)
{
    if (!m_page)
        return Exception { INVALID_ACCESS_ERR };
    settings().setForcePendingWebGLPolicy(forced);
    return { };
}

ExceptionOr<void> InternalSettings::setTimeWithoutMouseMovementBeforeHidingControls(double time)
{
    if (!m_page)
        return Exception { INVALID_ACCESS_ERR };
    settings().setTimeWithoutMouseMovementBeforeHidingControls(Seconds { time });
    return { };
}

ExceptionOr<void> InternalSettings::setUseLegacyBackgroundSizeShorthandBehavior(bool enabled)
{
    if (!m_page)
        return Exception { INVALID_ACCESS_ERR };
    settings().setUseLegacyBackgroundSizeShorthandBehavior(enabled);
    return { };
}

ExceptionOr<void> InternalSettings::setAutoscrollForDragAndDropEnabled(bool enabled)
{
    if (!m_page)
        return Exception { INVALID_ACCESS_ERR };
    settings().setAutoscrollForDragAndDropEnabled(enabled);
    return { };
}

ExceptionOr<void> InternalSettings::setFontFallbackPrefersPictographs(bool preferPictographs)
{
    if (!m_page)
        return Exception { INVALID_ACCESS_ERR };
    settings().setFontFallbackPrefersPictographs(preferPictographs);
    return { };
}

ExceptionOr<void> InternalSettings::setWebFontsAlwaysFallBack(bool enable)
{
    if (!m_page)
        return Exception { INVALID_ACCESS_ERR };
    settings().setWebFontsAlwaysFallBack(enable);
    return { };
}

ExceptionOr<void> InternalSettings::setQuickTimePluginReplacementEnabled(bool enabled)
{
    if (!m_page)
        return Exception { INVALID_ACCESS_ERR };
    settings().setQuickTimePluginReplacementEnabled(enabled);
    return { };
}

ExceptionOr<void> InternalSettings::setYouTubeFlashPluginReplacementEnabled(bool enabled)
{
    if (!m_page)
        return Exception { INVALID_ACCESS_ERR };
    settings().setYouTubeFlashPluginReplacementEnabled(enabled);
    return { };
}

ExceptionOr<void> InternalSettings::setBackgroundShouldExtendBeyondPage(bool hasExtendedBackground)
{
    if (!m_page)
        return Exception { INVALID_ACCESS_ERR };
    settings().setBackgroundShouldExtendBeyondPage(hasExtendedBackground);
    return { };
}

ExceptionOr<void> InternalSettings::setShouldConvertPositionStyleOnCopy(bool convert)
{
    if (!m_page)
        return Exception { INVALID_ACCESS_ERR };
    settings().setShouldConvertPositionStyleOnCopy(convert);
    return { };
}

ExceptionOr<void> InternalSettings::setScrollingTreeIncludesFrames(bool enabled)
{
    if (!m_page)
        return Exception { INVALID_ACCESS_ERR };
    settings().setScrollingTreeIncludesFrames(enabled);
    return { };
}

ExceptionOr<void> InternalSettings::setAllowUnclampedScrollPosition(bool allowUnclamped)
{
    if (!m_page || !m_page->mainFrame().view())
        return Exception { INVALID_ACCESS_ERR };

    m_page->mainFrame().view()->setAllowsUnclampedScrollPositionForTesting(allowUnclamped);
    return { };
}

ExceptionOr<void> InternalSettings::setAllowsInlineMediaPlayback(bool allows)
{
    if (!m_page)
        return Exception { INVALID_ACCESS_ERR };
    settings().setAllowsInlineMediaPlayback(allows);
    return { };
}

ExceptionOr<void> InternalSettings::setAllowsInlineMediaPlaybackAfterFullscreen(bool allows)
{
    if (!m_page)
        return Exception { INVALID_ACCESS_ERR };
    settings().setAllowsInlineMediaPlaybackAfterFullscreen(allows);
    return { };
}

ExceptionOr<void> InternalSettings::setInlineMediaPlaybackRequiresPlaysInlineAttribute(bool requires)
{
    if (!m_page)
        return Exception { INVALID_ACCESS_ERR };
    settings().setInlineMediaPlaybackRequiresPlaysInlineAttribute(requires);
    return { };
}

void InternalSettings::setIndexedDBWorkersEnabled(bool enabled)
{
#if ENABLE(INDEXED_DATABASE_IN_WORKERS)
    RuntimeEnabledFeatures::sharedFeatures().setIndexedDBWorkersEnabled(enabled);
#else
    UNUSED_PARAM(enabled);
#endif
}

void InternalSettings::setCSSGridLayoutEnabled(bool enabled)
{
    RuntimeEnabledFeatures::sharedFeatures().setCSSGridLayoutEnabled(enabled);
}

void InternalSettings::setWebGL2Enabled(bool enabled)
{
#if ENABLE(WEBGL2)
    RuntimeEnabledFeatures::sharedFeatures().setWebGL2Enabled(enabled);
#else
    UNUSED_PARAM(enabled);
#endif
}

void InternalSettings::setWebGPUEnabled(bool enabled)
{
#if ENABLE(WEBGPU)
    RuntimeEnabledFeatures::sharedFeatures().setWebGPUEnabled(enabled);
#else
    UNUSED_PARAM(enabled);
#endif
}

bool InternalSettings::shouldDispatchRequestAnimationFrameEvents()
{
    return settings().shouldDispatchRequestAnimationFrameEvents();
}

void InternalSettings::setShouldDispatchRequestAnimationFrameEvents(bool shouldDispatchRequestAnimationFrameEvents)
{
    settings().setShouldDispatchRequestAnimationFrameEvents(shouldDispatchRequestAnimationFrameEvents);
}

ExceptionOr<String> InternalSettings::userInterfaceDirectionPolicy()
{
    if (!m_page)
        return Exception { INVALID_ACCESS_ERR };
    switch (settings().userInterfaceDirectionPolicy()) {
    case UserInterfaceDirectionPolicy::Content:
        return String { ASCIILiteral { "Content" } };
    case UserInterfaceDirectionPolicy::System:
        return String { ASCIILiteral { "View" } };
    }
    ASSERT_NOT_REACHED();
    return Exception { INVALID_ACCESS_ERR };
}

ExceptionOr<void> InternalSettings::setUserInterfaceDirectionPolicy(const String& policy)
{
    if (!m_page)
        return Exception { INVALID_ACCESS_ERR };
    if (equalLettersIgnoringASCIICase(policy, "content")) {
        settings().setUserInterfaceDirectionPolicy(UserInterfaceDirectionPolicy::Content);
        return { };
    }
    if (equalLettersIgnoringASCIICase(policy, "view")) {
        settings().setUserInterfaceDirectionPolicy(UserInterfaceDirectionPolicy::System);
        return { };
    }
    return Exception { INVALID_ACCESS_ERR };
}

ExceptionOr<String> InternalSettings::systemLayoutDirection()
{
    if (!m_page)
        return Exception { INVALID_ACCESS_ERR };
    switch (settings().systemLayoutDirection()) {
    case LTR:
        return String { ASCIILiteral { "LTR" } };
    case RTL:
        return String { ASCIILiteral { "RTL" } };
    }
    ASSERT_NOT_REACHED();
    return Exception { INVALID_ACCESS_ERR };
}

ExceptionOr<void> InternalSettings::setSystemLayoutDirection(const String& direction)
{
    if (!m_page)
        return Exception { INVALID_ACCESS_ERR };
    if (equalLettersIgnoringASCIICase(direction, "ltr")) {
        settings().setSystemLayoutDirection(LTR);
        return { };
    }
    if (equalLettersIgnoringASCIICase(direction, "rtl")) {
        settings().setSystemLayoutDirection(RTL);
        return { };
    }
    return Exception { INVALID_ACCESS_ERR };
}

void InternalSettings::setAllowsAnySSLCertificate(bool allowsAnyCertificate)
{
    Settings::setAllowsAnySSLCertificate(allowsAnyCertificate);
#if USE(SOUP)
    SoupNetworkSession::setShouldIgnoreTLSErrors(allowsAnyCertificate);
#endif
}

ExceptionOr<bool> InternalSettings::deferredCSSParserEnabled()
{
    if (!m_page)
        return Exception { INVALID_ACCESS_ERR };
    return settings().deferredCSSParserEnabled();
}

ExceptionOr<void> InternalSettings::setDeferredCSSParserEnabled(bool enabled)
{
    if (!m_page)
        return Exception { INVALID_ACCESS_ERR };
    settings().setDeferredCSSParserEnabled(enabled);
    return { };
}

static InternalSettings::ForcedAccessibilityValue settingsToInternalSettingsValue(Settings::ForcedAccessibilityValue value)
{
    switch (value) {
    case Settings::ForcedAccessibilityValue::System:
        return InternalSettings::ForcedAccessibilityValue::System;
    case Settings::ForcedAccessibilityValue::On:
        return InternalSettings::ForcedAccessibilityValue::On;
    case Settings::ForcedAccessibilityValue::Off:
        return InternalSettings::ForcedAccessibilityValue::Off;
    }

    ASSERT_NOT_REACHED();
    return InternalSettings::ForcedAccessibilityValue::Off;
}

static Settings::ForcedAccessibilityValue internalSettingsToSettingsValue(InternalSettings::ForcedAccessibilityValue value)
{
    switch (value) {
    case InternalSettings::ForcedAccessibilityValue::System:
        return Settings::ForcedAccessibilityValue::System;
    case InternalSettings::ForcedAccessibilityValue::On:
        return Settings::ForcedAccessibilityValue::On;
    case InternalSettings::ForcedAccessibilityValue::Off:
        return Settings::ForcedAccessibilityValue::Off;
    }

    ASSERT_NOT_REACHED();
    return Settings::ForcedAccessibilityValue::Off;
}

InternalSettings::ForcedAccessibilityValue InternalSettings::forcedColorsAreInvertedAccessibilityValue() const
{
    return settingsToInternalSettingsValue(settings().forcedColorsAreInvertedAccessibilityValue());
}

void InternalSettings::setForcedColorsAreInvertedAccessibilityValue(InternalSettings::ForcedAccessibilityValue value)
{
    settings().setForcedColorsAreInvertedAccessibilityValue(internalSettingsToSettingsValue(value));
}

InternalSettings::ForcedAccessibilityValue InternalSettings::forcedDisplayIsMonochromeAccessibilityValue() const
{
    return settingsToInternalSettingsValue(settings().forcedDisplayIsMonochromeAccessibilityValue());
}

void InternalSettings::setForcedDisplayIsMonochromeAccessibilityValue(InternalSettings::ForcedAccessibilityValue value)
{
    settings().setForcedDisplayIsMonochromeAccessibilityValue(internalSettingsToSettingsValue(value));
}

InternalSettings::ForcedAccessibilityValue InternalSettings::forcedPrefersReducedMotionAccessibilityValue() const
{
    return settingsToInternalSettingsValue(settings().forcedPrefersReducedMotionAccessibilityValue());
}

void InternalSettings::setForcedPrefersReducedMotionAccessibilityValue(InternalSettings::ForcedAccessibilityValue value)
{
    settings().setForcedPrefersReducedMotionAccessibilityValue(internalSettingsToSettingsValue(value));
}

// If you add to this class, make sure that you update the Backup class for test reproducability!

}
