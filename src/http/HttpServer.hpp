#pragma once

#include "AuthManager.hpp"

#include <memory>
#include <string>
#include <thread>

namespace httplib
{
    class SSLServer;
}

class IEptzController;

class HttpServer
{
public:
    // eptz must outlive this HttpServer.
    // cert/key are loaded at construction; failure is reported by start().
    HttpServer(IEptzController &eptz,
               const std::string &cert_file,
               const std::string &key_file,
               const std::string &users_file,
               int port = 8443);

    ~HttpServer();

    HttpServer(const HttpServer &) = delete;
    HttpServer &operator=(const HttpServer &) = delete;

    bool start(); // bind synchronously, then start listening thread (call once only)
    void stop();  // stop server and join thread

private:
    IEptzController &eptz_;

    std::unique_ptr<httplib::SSLServer> server_;

    int port_;
    std::thread thread_;

    AuthManager auth_manager_;
    bool auth_loaded_ = false;
};
