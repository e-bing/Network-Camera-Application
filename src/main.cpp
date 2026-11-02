#include "pipeline/EptzController.hpp"
#include "rtsp/RtspServer.hpp"

#include <gst/gst.h>
#include <cstdio>
#include <string>
#include <cstring>
#include <ifaddrs.h>
#include <arpa/inet.h>
#include <netinet/in.h>

static std::string get_local_ip()
{
    struct ifaddrs *ifaddr = nullptr;

    if (getifaddrs(&ifaddr) == -1)
        return "";

    std::string ip;

    for (struct ifaddrs *ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next)
    {
        if (ifa->ifa_addr == nullptr)
            continue;

        if (ifa->ifa_addr->sa_family != AF_INET)
            continue;

        auto *addr =
            reinterpret_cast<struct sockaddr_in *>(ifa->ifa_addr);

        char address[INET_ADDRSTRLEN];

        if (inet_ntop(AF_INET, &addr->sin_addr,
                      address, sizeof(address)) == nullptr)
            continue;

        if (strcmp(address, "127.0.0.1") == 0)
            continue;

        ip = address;
        break;
    }

    freeifaddrs(ifaddr);

    return ip;
}

// ePTZ 동작 확인용, HTTP API 붙이면 삭제 예정
static gboolean on_ptz_timer(gpointer user_data)
{
    auto *eptz = static_cast<EptzController *>(user_data);
    static const PtzState steps[] = {
        {0.0, 0.0, 1.0},   // 전체
        {0.0, 0.0, 2.0},   // 중앙 2배
        {-1.0, -1.0, 2.0}, // 좌상단
        {1.0, 1.0, 2.0},   // 우하단
    };
    static size_t i = 0;
    const PtzState &s = steps[i++ % 4];
    g_print("[ePTZ] timer pan=%.1f tilt=%.1f zoom=%.1f -> %s\n",
            s.pan, s.tilt, s.zoom,
            eptz->setPtz(s) == PtzResult::Ok ? "Ok" : "Invalid");
    return G_SOURCE_CONTINUE;
}

int main(int argc, char *argv[])
{
    gst_init(&argc, &argv);

    GMainLoop *loop = g_main_loop_new(nullptr, FALSE);
    EptzController eptz(1280, 720);
    RtspServer rtsp(eptz, "8554");
    if (!rtsp.start())
    {
        std::fprintf(stderr, "Failed to start RTSP server\n");
        return 1;
    }

    const std::string ip = get_local_ip();

    if (ip.empty())
    {
        std::printf(
            "RTSP server ready, but local IP could not be determined.\n");
    }
    else
    {
        std::printf(
            "RTSP server ready: rtsp://%s:8554/stream\n",
            ip.c_str());
    }

    g_timeout_add_seconds(5, on_ptz_timer, &eptz);
    g_main_loop_run(loop);

    g_main_loop_unref(loop);
    return 0;
}