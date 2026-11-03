#include "HttpServer.hpp"
#include "../pipeline/IEptzController.hpp"

#include <httplib.h>

#include <iostream>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

HttpServer::HttpServer(IEptzController &eptz,
                       const std::string *cert_file,
                       const std::string *key_file,
                       int port)
    : eptz_(eptz),
      server_(std::make_unique<httplib::SSLServer>(
          cert_file, key_file)),
      port_(port)
{
    server_->Get("/api/v1/ptz",
                 [this](const httplib::Request &,
                        httplib::Response &res)
                 {
                     const PtzState state = eptz_.getPtz();

                     json body = {
                         {"pan", state.pan},
                         {"tilt", state.tilt},
                         {"zoom", state.zoom}};

                     res.set_content(body.dump(), "application/json");
                 });
}

HttpServer::~HttpServer()
{
    stop();
}

bool HttpServer::start()
{
    if (thread_.joinable())
        return false;

    if (!server_->is_valid())
    {
        std::cerr << "[HTTPS] SSL server initialization failed\n";
        return false;
    }

    if (!server_->bind_to_port("0.0.0.0", port_))
    {
        std::cerr << "[HTTPS] Failed to bind port " << port_ << '\n';
        return false;
    }

    thread_ = std::thread([this]()
                          {
                              if (!server_->listen_after_bind())
                                  std::cerr << "[HTTPS] Listen failed\n"; });

    server_->wait_until_ready();

    return true;
}

void HttpServer::stop()
{
    server_->stop();

    if (thread_.joinable())
        thread_.join();
}
