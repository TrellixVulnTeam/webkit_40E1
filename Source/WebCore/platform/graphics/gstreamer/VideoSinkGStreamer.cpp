/*
 *  Copyright (C) 2007 OpenedHand
 *  Copyright (C) 2007 Alp Toker <alp@atoker.com>
 *  Copyright (C) 2009, 2010, 2011, 2012, 2015, 2016 Igalia S.L
 *  Copyright (C) 2015, 2016 Metrological Group B.V.
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

/*
 *
 * WebKitVideoSink is a GStreamer sink element that triggers
 * repaints in the WebKit GStreamer media player for the
 * current video buffer.
 */

#include "config.h"
#include "VideoSinkGStreamer.h"

#if ENABLE(VIDEO) && USE(GSTREAMER)
#include "GRefPtrGStreamer.h"
#include "GStreamerUtilities.h"
#include "IntSize.h"
#include <glib.h>
#include <gst/gst.h>
#include <gst/video/gstvideometa.h>
#include <wtf/Condition.h>
#include <wtf/RunLoop.h>

using namespace WebCore;

// CAIRO_FORMAT_RGB24 used to render the video buffers is little/big endian dependant.
#if G_BYTE_ORDER == G_LITTLE_ENDIAN
#define GST_CAPS_FORMAT "{ BGRx, BGRA }"
#else
#define GST_CAPS_FORMAT "{ xRGB, ARGB }"
#endif

#define WEBKIT_VIDEO_SINK_PAD_CAPS GST_VIDEO_CAPS_MAKE_WITH_FEATURES(GST_CAPS_FEATURE_META_GST_VIDEO_GL_TEXTURE_UPLOAD_META, GST_CAPS_FORMAT) ";" GST_VIDEO_CAPS_MAKE(GST_CAPS_FORMAT)

static GstStaticPadTemplate s_sinkTemplate = GST_STATIC_PAD_TEMPLATE("sink", GST_PAD_SINK, GST_PAD_ALWAYS, GST_STATIC_CAPS(WEBKIT_VIDEO_SINK_PAD_CAPS));


GST_DEBUG_CATEGORY_STATIC(webkitVideoSinkDebug);
#define GST_CAT_DEFAULT webkitVideoSinkDebug

enum {
    REPAINT_REQUESTED,
    LAST_SIGNAL
};

static guint webkitVideoSinkSignals[LAST_SIGNAL] = { 0, };

static void webkitVideoSinkRepaintRequested(WebKitVideoSink*, GstSample*);
static GRefPtr<GstSample> webkitVideoSinkRequestRender(WebKitVideoSink*, GstBuffer*);

class VideoRenderRequestScheduler {
public:
    VideoRenderRequestScheduler()
#if !USE(COORDINATED_GRAPHICS_THREADED)
        : m_timer(RunLoop::main(), this, &VideoRenderRequestScheduler::render)
#endif
    {
    }

    void start()
    {
        LockHolder locker(m_sampleMutex);
        m_unlocked = false;
    }

    void stop()
    {
        LockHolder locker(m_sampleMutex);
        m_sample = nullptr;
        m_unlocked = true;
#if !USE(COORDINATED_GRAPHICS_THREADED)
        m_timer.stop();
        m_dataCondition.notifyOne();
#endif
    }

    void drain()
    {
        LockHolder locker(m_sampleMutex);
        m_sample = nullptr;
    }

    bool requestRender(WebKitVideoSink* sink, GstBuffer* buffer)
    {
        LockHolder locker(m_sampleMutex);
        if (m_unlocked)
            return true;

        m_sample = webkitVideoSinkRequestRender(sink, buffer);
        if (!m_sample)
            return false;

#if USE(COORDINATED_GRAPHICS_THREADED)
        auto sample = WTFMove(m_sample);
        locker.unlockEarly();
        if (LIKELY(GST_IS_SAMPLE(sample.get())))
            webkitVideoSinkRepaintRequested(sink, sample.get());
#else
        m_sink = sink;
        m_timer.startOneShot(0_s);
        m_dataCondition.wait(m_sampleMutex);
#endif
        return true;
    }

private:

#if !USE(COORDINATED_GRAPHICS_THREADED)
    void render()
    {
        LockHolder locker(m_sampleMutex);
        GRefPtr<GstSample> sample = WTFMove(m_sample);
        GRefPtr<WebKitVideoSink> sink = WTFMove(m_sink);
        if (sample && !m_unlocked && LIKELY(GST_IS_SAMPLE(sample.get())))
            webkitVideoSinkRepaintRequested(sink.get(), sample.get());
        m_dataCondition.notifyOne();
    }
#endif

    Lock m_sampleMutex;
    GRefPtr<GstSample> m_sample;

#if !USE(COORDINATED_GRAPHICS_THREADED)
    RunLoop::Timer<VideoRenderRequestScheduler> m_timer;
    Condition m_dataCondition;
    GRefPtr<WebKitVideoSink> m_sink;
#endif

    // If this is true all processing should finish ASAP
    // This is necessary because there could be a race between
    // unlock() and render(), where unlock() wins, signals the
    // Condition, then render() tries to render a frame although
    // everything else isn't running anymore. This will lead
    // to deadlocks because render() holds the stream lock.
    //
    // Protected by the sample mutex
    bool m_unlocked { false };
};

struct _WebKitVideoSinkPrivate {
    _WebKitVideoSinkPrivate()
    {
        gst_video_info_init(&info);
    }

    ~_WebKitVideoSinkPrivate()
    {
        if (currentCaps)
            gst_caps_unref(currentCaps);
    }

    VideoRenderRequestScheduler scheduler;
    GstVideoInfo info;
    GstCaps* currentCaps;
};

#define webkit_video_sink_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE(WebKitVideoSink, webkit_video_sink, GST_TYPE_VIDEO_SINK, GST_DEBUG_CATEGORY_INIT(webkitVideoSinkDebug, "webkitsink", 0, "webkit video sink"));


static void webkit_video_sink_init(WebKitVideoSink* sink)
{
    sink->priv = G_TYPE_INSTANCE_GET_PRIVATE(sink, WEBKIT_TYPE_VIDEO_SINK, WebKitVideoSinkPrivate);
    g_object_set(GST_BASE_SINK(sink), "enable-last-sample", FALSE, nullptr);
    new (sink->priv) WebKitVideoSinkPrivate();
}

static void webkitVideoSinkRepaintRequested(WebKitVideoSink* sink, GstSample* sample)
{
    g_signal_emit(sink, webkitVideoSinkSignals[REPAINT_REQUESTED], 0, sample);
}

static GRefPtr<GstSample> webkitVideoSinkRequestRender(WebKitVideoSink* sink, GstBuffer* buffer)
{
    WebKitVideoSinkPrivate* priv = sink->priv;
    GRefPtr<GstSample> sample = adoptGRef(gst_sample_new(buffer, priv->currentCaps, nullptr, nullptr));

    // The video info structure is valid only if the sink handled an allocation query.
    GstVideoFormat format = GST_VIDEO_INFO_FORMAT(&priv->info);
    if (format == GST_VIDEO_FORMAT_UNKNOWN)
        return nullptr;

#if !(USE(TEXTURE_MAPPER_GL) && !USE(COORDINATED_GRAPHICS))
    // Cairo's ARGB has pre-multiplied alpha while GStreamer's doesn't.
    // Here we convert to Cairo's ARGB.
    if (format == GST_VIDEO_FORMAT_ARGB || format == GST_VIDEO_FORMAT_BGRA) {
        // Because GstBaseSink::render() only owns the buffer reference in the
        // method scope we can't use gst_buffer_make_writable() here. Also
        // The buffer content should not be changed here because the same buffer
        // could be passed multiple times to this method (in theory).

        GstBuffer* newBuffer = WebCore::createGstBuffer(buffer);

        // Check if allocation failed.
        if (UNLIKELY(!newBuffer))
            return nullptr;

        // We don't use Color::premultipliedARGBFromColor() here because
        // one function call per video pixel is just too expensive:
        // For 720p/PAL for example this means 1280*720*25=23040000
        // function calls per second!
        GstVideoFrame sourceFrame;
        GstVideoFrame destinationFrame;

        if (!gst_video_frame_map(&sourceFrame, &priv->info, buffer, GST_MAP_READ)) {
            gst_buffer_unref(newBuffer);
            return nullptr;
        }
        if (!gst_video_frame_map(&destinationFrame, &priv->info, newBuffer, GST_MAP_WRITE)) {
            gst_video_frame_unmap(&sourceFrame);
            gst_buffer_unref(newBuffer);
            return nullptr;
        }

        const guint8* source = static_cast<guint8*>(GST_VIDEO_FRAME_PLANE_DATA(&sourceFrame, 0));
        guint8* destination = static_cast<guint8*>(GST_VIDEO_FRAME_PLANE_DATA(&destinationFrame, 0));

        for (int x = 0; x < GST_VIDEO_FRAME_HEIGHT(&sourceFrame); x++) {
            for (int y = 0; y < GST_VIDEO_FRAME_WIDTH(&sourceFrame); y++) {
#if G_BYTE_ORDER == G_LITTLE_ENDIAN
                unsigned short alpha = source[3];
                destination[0] = (source[0] * alpha + 128) / 255;
                destination[1] = (source[1] * alpha + 128) / 255;
                destination[2] = (source[2] * alpha + 128) / 255;
                destination[3] = alpha;
#else
                unsigned short alpha = source[0];
                destination[0] = alpha;
                destination[1] = (source[1] * alpha + 128) / 255;
                destination[2] = (source[2] * alpha + 128) / 255;
                destination[3] = (source[3] * alpha + 128) / 255;
#endif
                source += 4;
                destination += 4;
            }
        }

        gst_video_frame_unmap(&sourceFrame);
        gst_video_frame_unmap(&destinationFrame);
        sample = adoptGRef(gst_sample_new(newBuffer, priv->currentCaps, nullptr, nullptr));
        gst_buffer_unref(newBuffer);
    }
#endif

    return sample;
}

static GstFlowReturn webkitVideoSinkRender(GstBaseSink* baseSink, GstBuffer* buffer)
{
    WebKitVideoSink* sink = WEBKIT_VIDEO_SINK(baseSink);
    return sink->priv->scheduler.requestRender(sink, buffer) ? GST_FLOW_OK : GST_FLOW_ERROR;
}

static void webkitVideoSinkFinalize(GObject* object)
{
    WEBKIT_VIDEO_SINK(object)->priv->~WebKitVideoSinkPrivate();
    G_OBJECT_CLASS(parent_class)->finalize(object);
}

static gboolean webkitVideoSinkUnlock(GstBaseSink* baseSink)
{
    WebKitVideoSinkPrivate* priv = WEBKIT_VIDEO_SINK(baseSink)->priv;

    priv->scheduler.stop();

    return GST_CALL_PARENT_WITH_DEFAULT(GST_BASE_SINK_CLASS, unlock, (baseSink), TRUE);
}

static gboolean webkitVideoSinkUnlockStop(GstBaseSink* baseSink)
{
    WebKitVideoSinkPrivate* priv = WEBKIT_VIDEO_SINK(baseSink)->priv;

    priv->scheduler.start();

    return GST_CALL_PARENT_WITH_DEFAULT(GST_BASE_SINK_CLASS, unlock_stop, (baseSink), TRUE);
}

static gboolean webkitVideoSinkStop(GstBaseSink* baseSink)
{
    WebKitVideoSinkPrivate* priv = WEBKIT_VIDEO_SINK(baseSink)->priv;

    priv->scheduler.stop();
    if (priv->currentCaps) {
        gst_caps_unref(priv->currentCaps);
        priv->currentCaps = nullptr;
    }

    return TRUE;
}

static gboolean webkitVideoSinkStart(GstBaseSink* baseSink)
{
    WebKitVideoSinkPrivate* priv = WEBKIT_VIDEO_SINK(baseSink)->priv;

    priv->scheduler.start();

    return TRUE;
}

static gboolean webkitVideoSinkSetCaps(GstBaseSink* baseSink, GstCaps* caps)
{
    WebKitVideoSink* sink = WEBKIT_VIDEO_SINK(baseSink);
    WebKitVideoSinkPrivate* priv = sink->priv;

    GST_DEBUG_OBJECT(sink, "Current caps %" GST_PTR_FORMAT ", setting caps %" GST_PTR_FORMAT, priv->currentCaps, caps);

    GstVideoInfo videoInfo;
    gst_video_info_init(&videoInfo);
    if (!gst_video_info_from_caps(&videoInfo, caps)) {
        GST_ERROR_OBJECT(sink, "Invalid caps %" GST_PTR_FORMAT, caps);
        return FALSE;
    }

    priv->info = videoInfo;
    gst_caps_replace(&priv->currentCaps, caps);
    return TRUE;
}

static gboolean webkitVideoSinkProposeAllocation(GstBaseSink* baseSink, GstQuery* query)
{
    GstCaps* caps;
    gst_query_parse_allocation(query, &caps, nullptr);
    if (!caps)
        return FALSE;

    WebKitVideoSink* sink = WEBKIT_VIDEO_SINK(baseSink);
    if (!gst_video_info_from_caps(&sink->priv->info, caps))
        return FALSE;

    gst_query_add_allocation_meta(query, GST_VIDEO_META_API_TYPE, nullptr);
    gst_query_add_allocation_meta(query, GST_VIDEO_CROP_META_API_TYPE, nullptr);
    gst_query_add_allocation_meta(query, GST_VIDEO_GL_TEXTURE_UPLOAD_META_API_TYPE, nullptr);
    return TRUE;
}

static gboolean webkitVideoSinkEvent(GstBaseSink* baseSink, GstEvent* event)
{
    switch (GST_EVENT_TYPE(event)) {
    case GST_EVENT_FLUSH_START: {
        WebKitVideoSink* sink = WEBKIT_VIDEO_SINK(baseSink);
        sink->priv->scheduler.drain();

        GST_DEBUG_OBJECT(sink, "Flush-start, releasing m_sample");
        }
        FALLTHROUGH;
    default:
        return GST_CALL_PARENT_WITH_DEFAULT(GST_BASE_SINK_CLASS, event, (baseSink, event), TRUE);
    }
}

static void webkit_video_sink_class_init(WebKitVideoSinkClass* klass)
{
    GObjectClass* gobjectClass = G_OBJECT_CLASS(klass);
    GstBaseSinkClass* baseSinkClass = GST_BASE_SINK_CLASS(klass);
    GstElementClass* elementClass = GST_ELEMENT_CLASS(klass);

    gst_element_class_add_pad_template(elementClass, gst_static_pad_template_get(&s_sinkTemplate));
    gst_element_class_set_metadata(elementClass, "WebKit video sink", "Sink/Video", "Sends video data from a GStreamer pipeline to WebKit", "Igalia, Alp Toker <alp@atoker.com>");

    g_type_class_add_private(klass, sizeof(WebKitVideoSinkPrivate));

    gobjectClass->finalize = webkitVideoSinkFinalize;

    baseSinkClass->unlock = webkitVideoSinkUnlock;
    baseSinkClass->unlock_stop = webkitVideoSinkUnlockStop;
    baseSinkClass->render = webkitVideoSinkRender;
    baseSinkClass->preroll = webkitVideoSinkRender;
    baseSinkClass->stop = webkitVideoSinkStop;
    baseSinkClass->start = webkitVideoSinkStart;
    baseSinkClass->set_caps = webkitVideoSinkSetCaps;
    baseSinkClass->propose_allocation = webkitVideoSinkProposeAllocation;
    baseSinkClass->event = webkitVideoSinkEvent;

    webkitVideoSinkSignals[REPAINT_REQUESTED] = g_signal_new("repaint-requested",
            G_TYPE_FROM_CLASS(klass),
            static_cast<GSignalFlags>(G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION),
            0, // Class offset
            0, // Accumulator
            0, // Accumulator data
            g_cclosure_marshal_generic,
            G_TYPE_NONE, // Return type
            1, // Only one parameter
            GST_TYPE_SAMPLE);
}


GstElement* webkitVideoSinkNew()
{
    return GST_ELEMENT(g_object_new(WEBKIT_TYPE_VIDEO_SINK, nullptr));
}

#endif // ENABLE(VIDEO) && USE(GSTREAMER)
