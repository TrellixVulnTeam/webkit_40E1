/*
 * Copyright (C) 2016 Apple Inc.  All rights reserved.
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
#include "ImageFrameCache.h"

#include "Image.h"
#include "ImageObserver.h"
#include "Logging.h"
#include <wtf/SystemTracing.h>

#if USE(CG)
#include "ImageDecoderCG.h"
#elif USE(DIRECT2D)
#include "ImageDecoderDirect2D.h"
#include <WinCodec.h>
#else
#include "ImageDecoder.h"
#endif

#include <wtf/CheckedArithmetic.h>
#include <wtf/MainThread.h>
#include <wtf/RunLoop.h>

namespace WebCore {

ImageFrameCache::ImageFrameCache(Image* image)
    : m_image(image)
{
}

ImageFrameCache::ImageFrameCache(NativeImagePtr&& nativeImage)
{
    m_frameCount = 1;
    m_encodedDataStatus = EncodedDataStatus::Complete;
    growFrames();

    setNativeImage(WTFMove(nativeImage));

    m_decodedSize = m_frames[0].frameBytes();

    // The assumption is the memory image will be displayed with the default
    // orientation. So set m_sizeRespectingOrientation to be the same as m_size.
    m_size = m_frames[0].size();
    m_sizeRespectingOrientation = m_size;
}

ImageFrameCache::~ImageFrameCache()
{
    ASSERT(!hasAsyncDecodingQueue());
}

void ImageFrameCache::setDecoder(ImageDecoder* decoder)
{
    if (m_decoder == decoder)
        return;

    // Changing the decoder has to stop the decoding thread. The current frame will
    // continue decoding safely because the decoding thread has its own
    // reference of the old decoder.
    stopAsyncDecodingQueue();
    m_decoder = decoder;
}

ImageDecoder* ImageFrameCache::decoder() const
{
    return m_decoder.get();
}

void ImageFrameCache::destroyDecodedData(size_t frameCount, size_t excludeFrame)
{
    unsigned decodedSize = 0;

    ASSERT(frameCount <= m_frames.size());

    for (size_t index = 0; index < frameCount; ++index) {
        if (index == excludeFrame)
            continue;
        decodedSize += m_frames[index].clearImage();
    }

    decodedSizeReset(decodedSize);
}

void ImageFrameCache::destroyIncompleteDecodedData()
{
    unsigned decodedSize = 0;
    
    for (auto& frame : m_frames) {
        if (!frame.hasMetadata() || frame.isComplete())
            continue;
        
        decodedSize += frame.clear();
    }

    decodedSizeDecreased(decodedSize);
}

void ImageFrameCache::decodedSizeChanged(long long decodedSize)
{
    if (!decodedSize || !m_image || !m_image->imageObserver())
        return;
    
    m_image->imageObserver()->decodedSizeChanged(m_image, decodedSize);
}

void ImageFrameCache::decodedSizeIncreased(unsigned decodedSize)
{
    if (!decodedSize)
        return;
    
    m_decodedSize += decodedSize;
    
    // The fully-decoded frame will subsume the partially decoded data used
    // to determine image properties.
    long long changeSize = static_cast<long long>(decodedSize) - m_decodedPropertiesSize;
    m_decodedPropertiesSize = 0;
    decodedSizeChanged(changeSize);
}

void ImageFrameCache::decodedSizeDecreased(unsigned decodedSize)
{
    if (!decodedSize)
        return;

    ASSERT(m_decodedSize >= decodedSize);
    m_decodedSize -= decodedSize;
    decodedSizeChanged(-static_cast<long long>(decodedSize));
}

void ImageFrameCache::decodedSizeReset(unsigned decodedSize)
{
    ASSERT(m_decodedSize >= decodedSize);
    m_decodedSize -= decodedSize;

    // Clearing the ImageSource destroys the extra decoded data used for
    // determining image properties.
    decodedSize += m_decodedPropertiesSize;
    m_decodedPropertiesSize = 0;
    decodedSizeChanged(-static_cast<long long>(decodedSize));
}

void ImageFrameCache::didDecodeProperties(unsigned decodedPropertiesSize)
{
    if (m_decodedSize)
        return;

    long long decodedSize = static_cast<long long>(decodedPropertiesSize) - m_decodedPropertiesSize;
    m_decodedPropertiesSize = decodedPropertiesSize;
    decodedSizeChanged(decodedSize);
}

void ImageFrameCache::growFrames()
{
    ASSERT(isSizeAvailable());
    ASSERT(m_frames.size() <= frameCount());
    m_frames.grow(frameCount());
}

void ImageFrameCache::setNativeImage(NativeImagePtr&& nativeImage)
{
    ASSERT(m_frames.size() == 1);
    ImageFrame& frame = m_frames[0];

    ASSERT(!isDecoderAvailable());

    frame.m_nativeImage = WTFMove(nativeImage);

    frame.m_decoding = ImageFrame::Decoding::Complete;
    frame.m_size = nativeImageSize(frame.m_nativeImage);
    frame.m_hasAlpha = nativeImageHasAlpha(frame.m_nativeImage);
}

void ImageFrameCache::cacheFrameMetadataAtIndex(size_t index, SubsamplingLevel subsamplingLevel)
{
    ASSERT(index < m_frames.size());
    ImageFrame& frame = m_frames[index];

    ASSERT(isDecoderAvailable());
    frame.m_decoding = m_decoder->frameIsCompleteAtIndex(index) ? ImageFrame::Decoding::Complete : ImageFrame::Decoding::Partial;
    if (frame.hasMetadata())
        return;
    
    frame.m_subsamplingLevel = subsamplingLevel;

    if (frame.m_decodingOptions.hasSizeForDrawing()) {
        ASSERT(frame.hasNativeImage());
        frame.m_size = nativeImageSize(frame.nativeImage());
    } else
        frame.m_size = m_decoder->frameSizeAtIndex(index, subsamplingLevel);

    frame.m_orientation = m_decoder->frameOrientationAtIndex(index);
    frame.m_hasAlpha = m_decoder->frameHasAlphaAtIndex(index);

    if (repetitionCount())
        frame.m_duration = m_decoder->frameDurationAtIndex(index);
}

void ImageFrameCache::cacheFrameNativeImageAtIndex(NativeImagePtr&& nativeImage, size_t index, SubsamplingLevel subsamplingLevel, const DecodingOptions& decodingOptions)
{
    ASSERT(index < m_frames.size());
    ImageFrame& frame = m_frames[index];

    // Clear the current image frame and update the observer with this clearance.
    decodedSizeDecreased(frame.clear());

    // Do not cache the NativeImage if adding its frameByes to the MemoryCache will cause numerical overflow.
    size_t frameBytes = size().unclampedArea() * sizeof(RGBA32);
    if (!WTF::isInBounds<unsigned>(frameBytes + decodedSize()))
        return;

    // Move the new image to the cache.
    frame.m_nativeImage = WTFMove(nativeImage);
    frame.m_decodingOptions = decodingOptions;
    cacheFrameMetadataAtIndex(index, subsamplingLevel);

    // Update the observer with the new image frame bytes.
    decodedSizeIncreased(frame.frameBytes());
}

void ImageFrameCache::cacheAsyncFrameNativeImageAtIndex(NativeImagePtr&& nativeImage, size_t index, SubsamplingLevel subsamplingLevel, const DecodingOptions& decodingOptions)
{
    if (!isDecoderAvailable())
        return;

    ASSERT(index < m_frames.size());
    ASSERT(!frameHasDecodedNativeImageCompatibleWithOptionsAtIndex(index, subsamplingLevel, decodingOptions));

    // Clean the old native image and set a new one
    cacheFrameNativeImageAtIndex(WTFMove(nativeImage), index, subsamplingLevel, decodingOptions);
    LOG(Images, "ImageFrameCache::%s - %p - url: %s [frame %ld has been cached]", __FUNCTION__, this, sourceURL().utf8().data(), index);

    // Notify the image with the readiness of the new frame NativeImage.
    if (m_image)
        m_image->newFrameNativeImageAvailableAtIndex(index);
}

Ref<WorkQueue> ImageFrameCache::decodingQueue()
{
    if (!m_decodingQueue)
        m_decodingQueue = WorkQueue::create("org.webkit.ImageDecoder", WorkQueue::Type::Serial, WorkQueue::QOS::Default);
    
    return *m_decodingQueue;
}

void ImageFrameCache::startAsyncDecodingQueue()
{
    if (hasAsyncDecodingQueue() || !isDecoderAvailable())
        return;

    m_frameRequestQueue.open();

    Ref<ImageFrameCache> protectedThis = Ref<ImageFrameCache>(*this);
    Ref<WorkQueue> protectedQueue = decodingQueue();
    Ref<ImageDecoder> protectedDecoder = Ref<ImageDecoder>(*m_decoder);

    // We need to protect this, m_decodingQueue and m_decoder from being deleted while we are in the decoding loop.
    decodingQueue()->dispatch([this, protectedThis = WTFMove(protectedThis), protectedQueue = WTFMove(protectedQueue), protectedDecoder = WTFMove(protectedDecoder)] {
        ImageFrameRequest frameRequest;

        while (m_frameRequestQueue.dequeue(frameRequest)) {
            TraceScope tracingScope(AsyncImageDecodeStart, AsyncImageDecodeEnd);

            // Get the frame NativeImage on the decoding thread.
            NativeImagePtr nativeImage = protectedDecoder->createFrameImageAtIndex(frameRequest.index, frameRequest.subsamplingLevel, frameRequest.decodingOptions);
            if (nativeImage)
                LOG(Images, "ImageFrameCache::%s - %p - url: %s [frame %ld has been decoded]", __FUNCTION__, this, sourceURL().utf8().data(), frameRequest.index);
            else
                LOG(Images, "ImageFrameCache::%s - %p - url: %s [decoding for frame %ld has failed]", __FUNCTION__, this, sourceURL().utf8().data(), frameRequest.index);

            // Update the cached frames on the main thread to avoid updating the MemoryCache from a different thread.
            callOnMainThread([this, protectedQueue = protectedQueue.copyRef(), nativeImage, frameRequest] () mutable {
                // The queue may be closed if after we got the frame NativeImage, stopAsyncDecodingQueue() was called
                if (protectedQueue.ptr() == m_decodingQueue) {
                    ASSERT(m_frameCommitQueue.first() == frameRequest);
                    m_frameCommitQueue.removeFirst();
                    cacheAsyncFrameNativeImageAtIndex(WTFMove(nativeImage), frameRequest.index, frameRequest.subsamplingLevel, frameRequest.decodingOptions);
                } else
                    LOG(Images, "ImageFrameCache::%s - %p - url: %s [frame %ld will not cached]", __FUNCTION__, this, sourceURL().utf8().data(), frameRequest.index);
            });
        }
    });
}

bool ImageFrameCache::requestFrameAsyncDecodingAtIndex(size_t index, SubsamplingLevel subsamplingLevel, const std::optional<IntSize>& sizeForDrawing)
{
    if (!isDecoderAvailable())
        return false;

    ASSERT(index < m_frames.size());

    // We need to coalesce multiple requests for decoding the same ImageFrame while it
    // is still being decoded. This may happen if the image rectangle is repainted
    // multiple times while the ImageFrame has not finished decoding.
    if (frameIsBeingDecodedAndIsCompatibleWithOptionsAtIndex(index, sizeForDrawing))
        return true;

    if (frameHasDecodedNativeImageCompatibleWithOptionsAtIndex(index, subsamplingLevel, sizeForDrawing))
        return false;

    if (!hasAsyncDecodingQueue())
        startAsyncDecodingQueue();

    LOG(Images, "ImageFrameCache::%s - %p - url: %s [enqueuing frame %ld for decoding]", __FUNCTION__, this, sourceURL().utf8().data(), index);
    m_frameRequestQueue.enqueue({ index, subsamplingLevel, sizeForDrawing });
    m_frameCommitQueue.append({ index, subsamplingLevel, sizeForDrawing });
    return true;
}

bool ImageFrameCache::isAsyncDecodingQueueIdle() const
{
    return m_frameCommitQueue.isEmpty();
}
    
void ImageFrameCache::stopAsyncDecodingQueue()
{
    if (!hasAsyncDecodingQueue())
        return;
    
    std::for_each(m_frameCommitQueue.begin(), m_frameCommitQueue.end(), [this](const ImageFrameRequest& frameRequest) {
        ImageFrame& frame = m_frames[frameRequest.index];
        if (!frame.isEmpty()) {
            LOG(Images, "ImageFrameCache::%s - %p - url: %s [decoding has been cancelled for frame %ld]", __FUNCTION__, this, sourceURL().utf8().data(), frameRequest.index);
            frame.clear();
        }
    });

    m_frameRequestQueue.close();
    m_frameCommitQueue.clear();
    m_decodingQueue = nullptr;
    LOG(Images, "ImageFrameCache::%s - %p - url: %s [decoding has been stopped]", __FUNCTION__, this, sourceURL().utf8().data());
}

const ImageFrame& ImageFrameCache::frameAtIndexCacheIfNeeded(size_t index, ImageFrame::Caching caching, const std::optional<SubsamplingLevel>& subsamplingLevel)
{
    ASSERT(index < m_frames.size());
    ImageFrame& frame = m_frames[index];
    if (!isDecoderAvailable() || frameIsBeingDecodedAndIsCompatibleWithOptionsAtIndex(index, DecodingMode::Asynchronous))
        return frame;
    
    SubsamplingLevel subsamplingLevelValue = subsamplingLevel ? subsamplingLevel.value() : frame.subsamplingLevel();

    switch (caching) {
    case ImageFrame::Caching::Metadata:
        // Retrieve the metadata from ImageDecoder if the ImageFrame isn't complete.
        if (frame.isComplete())
            break;
        cacheFrameMetadataAtIndex(index, subsamplingLevelValue);
        break;
            
    case ImageFrame::Caching::MetadataAndImage:
        // Cache the image and retrieve the metadata from ImageDecoder only if there was not valid image stored.
        if (frame.hasFullSizeNativeImage(subsamplingLevel))
            break;
        // We have to perform synchronous image decoding in this code. 
        NativeImagePtr nativeImage = m_decoder->createFrameImageAtIndex(index, subsamplingLevelValue);
        // Clean the old native image and set a new one.
        cacheFrameNativeImageAtIndex(WTFMove(nativeImage), index, subsamplingLevelValue, DecodingMode::Synchronous);
        break;
    }

    return frame;
}

void ImageFrameCache::clearMetadata()
{
    m_frameCount = std::nullopt;
    m_singlePixelSolidColor = std::nullopt;
}

String ImageFrameCache::sourceURL() const
{
    return m_image ? m_image->sourceURL() : emptyString();
}

template<typename T, T (ImageDecoder::*functor)() const>
T ImageFrameCache::metadata(const T& defaultValue, std::optional<T>* cachedValue)
{
    if (cachedValue && *cachedValue)
        return cachedValue->value();

    if (!isDecoderAvailable() || !m_decoder->isSizeAvailable())
        return defaultValue;

    if (!cachedValue)
        return (*m_decoder.*functor)();

    *cachedValue = (*m_decoder.*functor)();
    didDecodeProperties(m_decoder->bytesDecodedToDetermineProperties());
    return cachedValue->value();
}

template<typename T, typename... Args>
T ImageFrameCache::frameMetadataAtIndex(size_t index, T (ImageFrame::*functor)(Args...) const, Args&&... args)
{
    const ImageFrame& frame = index < m_frames.size() ? m_frames[index] : ImageFrame::defaultFrame();
    return (frame.*functor)(std::forward<Args>(args)...);
}

template<typename T, typename... Args>
T ImageFrameCache::frameMetadataAtIndexCacheIfNeeded(size_t index, T (ImageFrame::*functor)() const, std::optional<T>* cachedValue, Args&&... args)
{
    if (cachedValue && *cachedValue)
        return cachedValue->value();

    const ImageFrame& frame = index < m_frames.size() ? frameAtIndexCacheIfNeeded(index, std::forward<Args>(args)...) : ImageFrame::defaultFrame();

    // Don't cache any unavailable frame metadata.
    if (!frame.hasMetadata() || !cachedValue)
        return (frame.*functor)();

    *cachedValue = (frame.*functor)();
    return cachedValue->value();
}

EncodedDataStatus ImageFrameCache::encodedDataStatus()
{
    if (m_encodedDataStatus)
        return m_encodedDataStatus.value();
    
    if (!isDecoderAvailable())
        return EncodedDataStatus::Unknown;
    
    EncodedDataStatus status = m_decoder->encodedDataStatus();
    if (status < EncodedDataStatus::SizeAvailable)
        return status;

    didDecodeProperties(m_decoder->bytesDecodedToDetermineProperties());
    if (status < EncodedDataStatus::Complete)
        return status;
    
    m_encodedDataStatus = status;
    return status;
}

size_t ImageFrameCache::frameCount()
{
    return metadata<size_t, (&ImageDecoder::frameCount)>(m_frames.size(), &m_frameCount);
}

RepetitionCount ImageFrameCache::repetitionCount()
{
    return metadata<RepetitionCount, (&ImageDecoder::repetitionCount)>(RepetitionCountNone, &m_repetitionCount);
}

String ImageFrameCache::filenameExtension()
{
    return metadata<String, (&ImageDecoder::filenameExtension)>(String(), &m_filenameExtension);
}

std::optional<IntPoint> ImageFrameCache::hotSpot()
{
    return metadata<std::optional<IntPoint>, (&ImageDecoder::hotSpot)>(std::nullopt, &m_hotSpot);
}

IntSize ImageFrameCache::size()
{
    return frameMetadataAtIndexCacheIfNeeded<IntSize>(0, (&ImageFrame::size), &m_size, ImageFrame::Caching::Metadata, SubsamplingLevel::Default);
}

IntSize ImageFrameCache::sizeRespectingOrientation()
{
    return frameMetadataAtIndexCacheIfNeeded<IntSize>(0, (&ImageFrame::sizeRespectingOrientation), &m_sizeRespectingOrientation, ImageFrame::Caching::Metadata, SubsamplingLevel::Default);
}

Color ImageFrameCache::singlePixelSolidColor()
{
    if (!m_singlePixelSolidColor && (size() != IntSize(1, 1) || frameCount() != 1))
        m_singlePixelSolidColor = Color();

    if (m_singlePixelSolidColor)
        return m_singlePixelSolidColor.value();

    return frameMetadataAtIndexCacheIfNeeded<Color>(0, (&ImageFrame::singlePixelSolidColor), &m_singlePixelSolidColor, ImageFrame::Caching::MetadataAndImage);
}

bool ImageFrameCache::frameIsBeingDecodedAndIsCompatibleWithOptionsAtIndex(size_t index, const DecodingOptions& decodingOptions)
{
    auto it = std::find_if(m_frameCommitQueue.begin(), m_frameCommitQueue.end(), [index, &decodingOptions](const ImageFrameRequest& frameRequest) {
        return frameRequest.index == index && frameRequest.decodingOptions.isAsynchronousCompatibleWith(decodingOptions);
    });
    return it != m_frameCommitQueue.end();
}

bool ImageFrameCache::frameIsCompleteAtIndex(size_t index)
{
    return frameMetadataAtIndex<bool>(index, (&ImageFrame::isComplete));
}

bool ImageFrameCache::frameHasAlphaAtIndex(size_t index)
{
    return frameMetadataAtIndex<bool>(index, (&ImageFrame::hasAlpha));
}

bool ImageFrameCache::frameHasFullSizeNativeImageAtIndex(size_t index, const std::optional<SubsamplingLevel>& subsamplingLevel)
{
    return frameMetadataAtIndex<bool>(index, (&ImageFrame::hasFullSizeNativeImage), subsamplingLevel);
}

bool ImageFrameCache::frameHasDecodedNativeImageCompatibleWithOptionsAtIndex(size_t index, const std::optional<SubsamplingLevel>& subsamplingLevel, const DecodingOptions& decodingOptions)
{
    return frameMetadataAtIndex<bool>(index, (&ImageFrame::hasDecodedNativeImageCompatibleWithOptions), subsamplingLevel, decodingOptions);
}
    
SubsamplingLevel ImageFrameCache::frameSubsamplingLevelAtIndex(size_t index)
{
    return frameMetadataAtIndex<SubsamplingLevel>(index, (&ImageFrame::subsamplingLevel));
}

IntSize ImageFrameCache::frameSizeAtIndex(size_t index, SubsamplingLevel subsamplingLevel)
{
    return frameMetadataAtIndexCacheIfNeeded<IntSize>(index, (&ImageFrame::size), nullptr, ImageFrame::Caching::Metadata, subsamplingLevel);
}

unsigned ImageFrameCache::frameBytesAtIndex(size_t index, SubsamplingLevel subsamplingLevel)
{
    return frameMetadataAtIndexCacheIfNeeded<unsigned>(index, (&ImageFrame::frameBytes), nullptr, ImageFrame::Caching::Metadata, subsamplingLevel);
}

float ImageFrameCache::frameDurationAtIndex(size_t index)
{
    return frameMetadataAtIndexCacheIfNeeded<float>(index, (&ImageFrame::duration), nullptr, ImageFrame::Caching::Metadata);
}

ImageOrientation ImageFrameCache::frameOrientationAtIndex(size_t index)
{
    return frameMetadataAtIndexCacheIfNeeded<ImageOrientation>(index, (&ImageFrame::orientation), nullptr, ImageFrame::Caching::Metadata);
}

NativeImagePtr ImageFrameCache::frameImageAtIndex(size_t index)
{
    return frameMetadataAtIndex<NativeImagePtr>(index, (&ImageFrame::nativeImage));
}

NativeImagePtr ImageFrameCache::frameImageAtIndexCacheIfNeeded(size_t index, SubsamplingLevel subsamplingLevel)
{
    return frameMetadataAtIndexCacheIfNeeded<NativeImagePtr>(index, (&ImageFrame::nativeImage), nullptr, ImageFrame::Caching::MetadataAndImage, subsamplingLevel);
}

}
