/*
 * Copyright (C) 2017 Apple Inc. All rights reserved.
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

#pragma once

#if ENABLE(MEDIA_STREAM) && PLATFORM(MAC)

#include "CaptureDevice.h"
#include "CaptureDeviceManager.h"
#include <CoreAudio/CoreAudio.h>
#include <wtf/HashMap.h>
#include <wtf/RefPtr.h>
#include <wtf/text/WTFString.h>

namespace WebCore {

class CoreAudioCaptureDevice;

class CoreAudioCaptureDeviceManager final : public CaptureDeviceManager {
    friend class NeverDestroyed<CoreAudioCaptureDeviceManager>;
public:
    static CoreAudioCaptureDeviceManager& singleton();

    Vector<CaptureDevice>& captureDevices() final;

    Vector<Ref<CoreAudioCaptureDevice>>& coreAudioCaptureDevices();

private:
    CoreAudioCaptureDeviceManager() = default;
    ~CoreAudioCaptureDeviceManager() = default;
    
    static OSStatus devicesChanged(AudioObjectID, UInt32, const AudioObjectPropertyAddress*, void*);

    void refreshAudioCaptureDevices();

    Vector<CaptureDevice> m_devices;
    Vector<Ref<CoreAudioCaptureDevice>> m_coreAudioCaptureDevices;
};

} // namespace WebCore

#endif // ENABLE(MEDIA_STREAM && PLATFORM(MAC)
