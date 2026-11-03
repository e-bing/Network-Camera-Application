#include "RtspServer.hpp"
#include "../pipeline/EptzController.hpp"

#include <gst/gst.h>
#include <gst/rtsp-server/rtsp-server.h>

static const char *PIPELINE =
    "libcamerasrc ! video/x-raw,width=1280,height=720,format=NV12,framerate=30/1 ! "
    "videocrop name=ptzcrop ! "
    "videoscale ! video/x-raw,width=640,height=360,pixel-aspect-ratio=1/1 ! "
    "v4l2h264enc ! video/x-h264,level=(string)4,profile=(string)baseline ! "
    "h264parse ! rtph264pay name=pay0 pt=96 config-interval=-1";

static GstRTSPFilterResult remove_all_clients(GstRTSPServer *, GstRTSPClient *, gpointer)
{
    return GST_RTSP_FILTER_REMOVE;
}

static void on_unprepared(GstRTSPMedia *media, gpointer user_data)
{
    auto *ctrl = static_cast<EptzController *>(user_data);
    auto *crop = static_cast<GstElement *>(
        g_object_get_data(G_OBJECT(media), "ptzcrop"));
    g_print("[ePTZ] unprepared media=%p crop=%p\n", media, crop);
    ctrl->detach(crop);
}

static void on_media_configure(GstRTSPMediaFactory *, GstRTSPMedia *media,
                               gpointer user_data)
{
    auto *ctrl = static_cast<EptzController *>(user_data);

    GstElement *bin = gst_rtsp_media_get_element(media);             // ref +1
    GstElement *crop = gst_bin_get_by_name(GST_BIN(bin), "ptzcrop"); // ref +1
    gst_object_unref(bin);

    if (crop == nullptr)
    {
        g_printerr("[ePTZ] ptzcrop not found\n");
        return;
    }

    ctrl->attach(crop);                                  // 내부에서 gst_object_ref
    g_object_set_data(G_OBJECT(media), "ptzcrop", crop); // 비교용, ref 아님
    g_print("[ePTZ] attach media=%p crop=%p refcount=%d\n",
            media, crop, GST_OBJECT_REFCOUNT_VALUE(crop));
    gst_object_unref(crop);

    g_signal_connect(media, "unprepared", G_CALLBACK(on_unprepared), ctrl);
}

RtspServer::RtspServer(EptzController &eptz, const char *port)
    : eptz_(eptz),
      server_(gst_rtsp_server_new())
{
    gst_rtsp_server_set_service(server_, port);

    GstRTSPMountPoints *mounts = gst_rtsp_server_get_mount_points(server_);
    GstRTSPMediaFactory *factory = gst_rtsp_media_factory_new();

    gst_rtsp_media_factory_set_launch(factory, PIPELINE);
    gst_rtsp_media_factory_set_shared(factory, TRUE);

    g_signal_connect(factory, "media-configure",
                     G_CALLBACK(on_media_configure), &eptz_);

    gst_rtsp_mount_points_add_factory(mounts, "/stream", factory);
    g_object_unref(mounts);
}

RtspServer::~RtspServer()
{
    if (source_id_ != 0)
        g_source_remove(source_id_);

    gst_rtsp_server_client_filter(server_, remove_all_clients, nullptr);

    g_object_unref(server_);
}

bool RtspServer::start()
{
    source_id_ = gst_rtsp_server_attach(server_, nullptr);
    return source_id_ != 0;
}