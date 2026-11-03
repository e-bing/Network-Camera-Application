#include "HttpServer.hpp"
#include "../pipeline/IEptzController.hpp"

#include <httplib.h>

#include <iostream>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

static json to_json(const PtzState &state)
{
    return {
        {"pan", state.pan},
        {"tilt", state.tilt},
        {"zoom", state.zoom}};
}

static void set_json_error(httplib::Response &res,
                           int status,
                           const char *error)
{
    res.status = status;
    res.set_content(
        json{{"error", error}}.dump(),
        "application/json");
}

HttpServer::HttpServer(IEptzController &eptz,
                       const std::string &cert_file,
                       const std::string &key_file,
                       const std::string &users_file,
                       int port)
    : eptz_(eptz),
      server_(std::make_unique<httplib::SSLServer>(
          cert_file.c_str(), key_file.c_str())),
      port_(port)
{
    auth_loaded_ = auth_manager_.load(users_file);

    server_->set_pre_routing_handler(
        [this](const httplib::Request &req, httplib::Response &res)
        {
            const auto role = auth_manager_.authenticate(
                req.get_header_value("Authorization"));

            if (!role.has_value())
            {
                set_json_error(res, 401, "unauthorized");
                res.set_header(
                    "WWW-Authenticate",
                    "Basic realm=\"camera\"");

                return httplib::Server::HandlerResponse::Handled;
            }

            const bool read_only = (req.method == "GET" || req.method == "HEAD");
            if (!read_only && *role != AuthManager::Role::Admin)
            {
                set_json_error(res, 403, "forbidden");
                return httplib::Server::HandlerResponse::Handled;
            }

            return httplib::Server::HandlerResponse::Unhandled;
        });

    server_->Get("/api/v1/ptz",
                 [this](const httplib::Request &,
                        httplib::Response &res)
                 {
                     const PtzState state = eptz_.getPtz();
                     res.set_content(to_json(state).dump(), "application/json");
                 });

    server_->Put("/api/v1/ptz",
                 [this](const httplib::Request &req,
                        httplib::Response &res)
                 {
                     const json body = json::parse(req.body, nullptr, false);

                     if (body.is_discarded())
                     {
                         set_json_error(res, 400, "invalid_json");
                         return;
                     }

                     if (!body.is_object() ||
                         !body.contains("pan") || !body.at("pan").is_number() ||
                         !body.contains("tilt") || !body.at("tilt").is_number() ||
                         !body.contains("zoom") || !body.at("zoom").is_number())
                     {
                         set_json_error(res, 400, "invalid_ptz_parameters");
                         return;
                     }

                     PtzState state{
                         body.at("pan").get<double>(),
                         body.at("tilt").get<double>(),
                         body.at("zoom").get<double>()};

                     if (eptz_.setPtz(state) != PtzResult::Ok)
                     {
                         set_json_error(res, 400, "invalid_ptz_value");
                         return;
                     }

                     res.status = 200;
                     res.set_content(
                         to_json(state).dump(),
                         "application/json");
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

    if (!auth_loaded_)
    {
        std::cerr << "[HTTPS] Failed to load user configuration\n";
        return false;
    }

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
