#include "AuthManager.hpp"

#include <openssl/evp.h>
#include <openssl/crypto.h>

#include <fstream>
#include <sstream>
#include <iostream>
#include <climits>

namespace
{
    constexpr int HASH_SIZE = 32;
    constexpr size_t MIN_SALT_SIZE = 16;
    constexpr int MAX_ITERATIONS = 10000000;
    constexpr int DEFAULT_ITERATIONS = 100000;

    std::vector<std::string> split(
        const std::string &str,
        char delimiter)
    {
        std::vector<std::string> result;
        std::stringstream ss(str);
        std::string item;

        while (std::getline(ss, item, delimiter))
            result.push_back(item);

        return result;
    }

    int hex_value(char c)
    {
        if (c >= '0' && c <= '9')
            return c - '0';

        if (c >= 'a' && c <= 'f')
            return c - 'a' + 10;

        if (c >= 'A' && c <= 'F')
            return c - 'A' + 10;

        return -1;
    }

    bool verify_password(
        const std::string &password,
        const std::vector<unsigned char> &salt,
        const std::vector<unsigned char> &hash,
        int iterations)
    {
        if (password.size() > static_cast<size_t>(INT_MAX) ||
            salt.empty() ||
            hash.size() != HASH_SIZE ||
            iterations <= 0)
            return false;

        unsigned char calculated_hash[HASH_SIZE];

        const int result = PKCS5_PBKDF2_HMAC(
            password.data(),
            static_cast<int>(password.size()),
            salt.data(),
            static_cast<int>(salt.size()),
            iterations,
            EVP_sha256(),
            HASH_SIZE,
            calculated_hash);

        if (result != 1)
            return false;

        return CRYPTO_memcmp(
                   calculated_hash,
                   hash.data(),
                   HASH_SIZE) == 0;
    }

    bool decode_base64(
        const std::string &encoded,
        std::string &decoded)
    {
        if (encoded.empty() || encoded.size() % 4 != 0)
            return false;

        for (size_t i = 0; i < encoded.size(); ++i)
        {
            const unsigned char c = encoded[i];

            const bool valid =
                (c >= 'A' && c <= 'Z') ||
                (c >= 'a' && c <= 'z') ||
                (c >= '0' && c <= '9') ||
                c == '+' || c == '/' || c == '=';

            if (!valid)
                return false;

            if (c == '=' && i < encoded.size() - 2)
                return false;
        }

        std::vector<unsigned char> buffer(
            (encoded.size() / 4) * 3);

        size_t padding = 0;

        if (encoded.back() == '=')
            ++padding;

        if (encoded.size() >= 2 &&
            encoded[encoded.size() - 2] == '=')
            ++padding;

        if (padding > 2)
            return false;

        for (size_t i = 0; i < encoded.size() - padding; ++i)
        {
            if (encoded[i] == '=')
                return false;
        }
        const int decoded_size = EVP_DecodeBlock(
            buffer.data(),
            reinterpret_cast<const unsigned char *>(encoded.data()),
            static_cast<int>(encoded.size()));

        if (decoded_size < 0)
            return false;

        const size_t actual_size =
            static_cast<size_t>(decoded_size) - padding;

        decoded.assign(
            reinterpret_cast<const char *>(buffer.data()),
            actual_size);

        return true;
    }

    bool decode_hex(
        const std::string &hex,
        std::vector<unsigned char> &decoded)
    {
        if (hex.empty() || hex.size() % 2 != 0)
            return false;

        decoded.clear();
        decoded.reserve(hex.size() / 2);

        for (size_t i = 0; i < hex.size(); i += 2)
        {
            const int high = hex_value(hex[i]);
            const int low = hex_value(hex[i + 1]);

            if (high < 0 || low < 0)
                return false;

            decoded.push_back(
                static_cast<unsigned char>((high << 4) | low));
        }

        return true;
    }
}

bool AuthManager::load(const std::string &config_file)
{
    std::ifstream file(config_file);

    if (!file.is_open())
    {
        std::cerr << "[AUTH] Failed to open " << config_file << '\n';
        return false;
    }

    std::unordered_map<std::string, User> loaded_users;
    std::string line;
    int line_number = 0;

    while (std::getline(file, line))
    {
        ++line_number;

        if (line.empty() || line[0] == '#')
            continue;

        const auto fields = split(line, ':');

        if (fields.size() != 5)
        {
            std::cerr << "[AUTH] Invalid config at line "
                      << line_number << '\n';
            return false;
        }

        const std::string &username = fields[0];
        const std::string &role = fields[1];

        if (username.empty() ||
            (role != "admin" && role != "viewer"))
        {
            std::cerr << "[AUTH] Invalid user or role at line "
                      << line_number << '\n';
            return false;
        }

        int iterations = 0;

        try
        {
            size_t pos = 0;
            iterations = std::stoi(fields[2], &pos);

            if (pos != fields[2].size())
            {
                std::cerr << "[AUTH] Invalid iterations at line "
                          << line_number << '\n';
                return false;
            }
        }
        catch (const std::exception &)
        {
            std::cerr << "[AUTH] Invalid iterations at line "
                      << line_number << '\n';
            return false;
        }

        if (iterations <= 0 || iterations > MAX_ITERATIONS)
        {
            std::cerr << "[AUTH] Iterations out of range at line "
                      << line_number << '\n';
            return false;
        }

        User user;
        user.role = (role == "admin")
                        ? Role::Admin
                        : Role::Viewer;
        user.iterations = iterations;

        if (!decode_hex(fields[3], user.salt) ||
            !decode_hex(fields[4], user.hash) ||
            user.salt.size() < MIN_SALT_SIZE ||
            user.hash.size() != HASH_SIZE)
        {
            std::cerr << "[AUTH] Invalid salt or hash at line "
                      << line_number << '\n';
            return false;
        }

        if (!loaded_users.emplace(username, std::move(user)).second)
        {
            std::cerr << "[AUTH] Duplicate username at line "
                      << line_number << '\n';
            return false;
        }
    }

    if (loaded_users.empty())
    {
        std::cerr << "[AUTH] No users configured\n";
        return false;
    }

    users_ = std::move(loaded_users);

    std::cout << "[AUTH] Loaded " << users_.size() << " user(s)\n";
    return true;
}

std::optional<AuthManager::Role> AuthManager::authenticate(
    const std::string &authorization) const
{
    constexpr char PREFIX[] = "Basic ";

    if (authorization.size() <= sizeof(PREFIX) - 1 ||
        authorization.size() > 4096)
        return std::nullopt;

    if (authorization.compare(0, sizeof(PREFIX) - 1, PREFIX) != 0)
        return std::nullopt;

    std::string decoded;

    if (!decode_base64(
            authorization.substr(sizeof(PREFIX) - 1),
            decoded))
        return std::nullopt;

    // 첫 번째 ':'를 기준으로 username과 password 분리
    const size_t separator = decoded.find(':');

    if (separator == std::string::npos)
        return std::nullopt;

    const std::string username = decoded.substr(0, separator);
    const std::string password = decoded.substr(separator + 1);

    if (username.empty())
        return std::nullopt;

    const auto it = users_.find(username);

    if (it == users_.end())
    {
        static const std::vector<unsigned char> dummy_salt(MIN_SALT_SIZE, 0);
        static const std::vector<unsigned char> dummy_hash(HASH_SIZE, 0);
        verify_password(password, dummy_salt, dummy_hash, DEFAULT_ITERATIONS);
        return std::nullopt;
    }

    if (!verify_password(
            password,
            it->second.salt,
            it->second.hash,
            it->second.iterations))
    {
        return std::nullopt;
    }

    return it->second.role;
}
