#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <optional>

class AuthManager
{
public:
    bool load(const std::string &config_file);

    enum class Role
    {
        Viewer,
        Admin
    };
    // load() must complete before the server starts; authenticate() is then safe to call concurrently.
    std::optional<Role> authenticate(const std::string &authorization) const; // nullopt → 401

private:
    struct User
    {
        Role role;
        int iterations = 0;
        std::vector<unsigned char> salt;
        std::vector<unsigned char> hash;
    };

    std::unordered_map<std::string, User> users_;
};