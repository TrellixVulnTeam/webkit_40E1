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

#include "PlatformAudioData.h"
#include <wtf/IteratorRange.h>
#include <wtf/RetainPtr.h>
#include <wtf/Vector.h>

struct AudioBuffer;
struct AudioBufferList;
typedef struct OpaqueCMBlockBuffer *CMBlockBufferRef;
typedef struct opaqueCMSampleBuffer *CMSampleBufferRef;

namespace WebCore {

class CAAudioStreamDescription;

class WebAudioBufferList : public PlatformAudioData {
public:
    WebAudioBufferList(const CAAudioStreamDescription&);
    WEBCORE_EXPORT WebAudioBufferList(const CAAudioStreamDescription&, uint32_t sampleCount);
    WebAudioBufferList(const CAAudioStreamDescription&, CMSampleBufferRef);

    AudioBufferList* list() const { return m_list.get(); }
    operator AudioBufferList&() const { return *m_list; }

    uint32_t bufferCount() const;
    AudioBuffer* buffer(uint32_t index) const;
    WTF::IteratorRange<AudioBuffer*> buffers() const;

private:
    Kind kind() const { return Kind::WebAudioBufferList; }

    size_t m_listBufferSize { 0 };
    std::unique_ptr<AudioBufferList> m_list;
    RetainPtr<CMBlockBufferRef> m_blockBuffer;
    Vector<uint8_t> m_flatBuffer;
};

}

SPECIALIZE_TYPE_TRAITS_BEGIN(WebCore::WebAudioBufferList)
static bool isType(const WebCore::PlatformAudioData& data) { return data.kind() == WebCore::PlatformAudioData::Kind::WebAudioBufferList; }
SPECIALIZE_TYPE_TRAITS_END()
