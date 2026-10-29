#pragma once

class EptzController;
struct _GstRTSPServer;

class RtspServer
{
public:
    // eptz must outlive this RtspServer.
    RtspServer(EptzController &eptz, const char *port);
    ~RtspServer();

    RtspServer(const RtspServer &) = delete;
    RtspServer &operator=(const RtspServer &) = delete;

    bool start(); // gst_rtsp_server_attach, 실패 시 false

private:
    EptzController &eptz_;
    _GstRTSPServer *server_ = nullptr;
    unsigned int source_id_ = 0;
};