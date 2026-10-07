#include <gst/gst.h>
#include <gst/rtsp-server/rtsp-server.h>
#include <cstdio>

static const char *PIPELINE =
    "libcamerasrc ! video/x-raw,width=1280,height=720,format=NV12 ! "
    "videoconvert ! x264enc tune=zerolatency bitrate=2000 speed-preset=ultrafast ! "
    "h264parse ! rtph264pay name=pay0 pt=96";

int main(int argc, char *argv[])
{
    gst_init(&argc, &argv);

    GMainLoop *loop = g_main_loop_new(nullptr, FALSE);
    GstRTSPServer *server = gst_rtsp_server_new();
    gst_rtsp_server_set_service(server, "8554");

    GstRTSPMountPoints *mounts = gst_rtsp_server_get_mount_points(server);
    GstRTSPMediaFactory *factory = gst_rtsp_media_factory_new();

    gst_rtsp_media_factory_set_launch(factory, PIPELINE);
    gst_rtsp_media_factory_set_shared(factory, TRUE); // 멀티클라이언트 공유

    gst_rtsp_mount_points_add_factory(mounts, "/stream", factory);
    g_object_unref(mounts);

    gst_rtsp_server_attach(server, nullptr);

    std::printf("RTSP server ready: rtsp://<pi-ip>:8554/stream\n");
    g_main_loop_run(loop);

    return 0;
}