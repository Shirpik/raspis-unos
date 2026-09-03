#include "auth.h"

#ifndef _WIN32
#error "Authentication crypto currently uses Windows CNG."
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <unordered_map>
#include <vector>

#include "json_utils.h"

namespace timetable {
namespace {

constexpr unsigned long long kPbkdf2Iterations = 150000;
constexpr int kSessionHours = 8;
const char* kInitialUsername = "yana_10";
const char* kInitialSalt = "d68b78bbec869da6b73b3f62104ca3c5";
const char* kInitialHash = "b148d0a796cebe8018f01cf005670ced314f506710d700b5817459b3139c3a2d";

struct Credentials {
    std::string username;
    std::string salt_hex;
    std::string hash_hex;
    unsigned long long iterations = kPbkdf2Iterations;
};

struct Session {
    std::string username;
    std::chrono::steady_clock::time_point expires;
};

std::mutex g_auth_mutex;
std::unordered_map<std::string, Session> g_sessions;

std::filesystem::path ConfigPath() {
    return std::filesystem::path("data") / "auth_config.json";
}

std::string Hex(const std::vector<unsigned char>& bytes) {
    static const char* digits = "0123456789abcdef";
    std::string out;
    out.reserve(bytes.size() * 2);
    for (unsigned char byte : bytes) {
        out.push_back(digits[(byte >> 4) & 0x0f]);
        out.push_back(digits[byte & 0x0f]);
    }
    return out;
}

int HexNibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool Unhex(const std::string& text, std::vector<unsigned char>& bytes) {
    if (text.empty() || text.size() % 2 != 0) return false;
    bytes.clear();
    bytes.reserve(text.size() / 2);
    for (size_t i = 0; i < text.size(); i += 2) {
        int hi = HexNibble(text[i]);
        int lo = HexNibble(text[i + 1]);
        if (hi < 0 || lo < 0) return false;
        bytes.push_back(static_cast<unsigned char>((hi << 4) | lo));
    }
    return true;
}

bool RandomBytes(size_t count, std::vector<unsigned char>& bytes) {
    bytes.resize(count);
    return BCryptGenRandom(nullptr, bytes.data(), static_cast<ULONG>(bytes.size()),
                           BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0;
}

bool DerivePasswordHash(const std::string& password, const std::string& salt_hex,
                        unsigned long long iterations, std::string& hash_hex) {
    std::vector<unsigned char> salt;
    if (!Unhex(salt_hex, salt)) return false;

    BCRYPT_ALG_HANDLE algorithm = nullptr;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr,
                                    BCRYPT_ALG_HANDLE_HMAC_FLAG) != 0) return false;

    std::vector<unsigned char> hash(32);
    NTSTATUS status = BCryptDeriveKeyPBKDF2(
        algorithm,
        reinterpret_cast<PUCHAR>(const_cast<char*>(password.data())),
        static_cast<ULONG>(password.size()), salt.data(), static_cast<ULONG>(salt.size()),
        iterations, hash.data(), static_cast<ULONG>(hash.size()), 0);
    BCryptCloseAlgorithmProvider(algorithm, 0);
    if (status != 0) return false;
    hash_hex = Hex(hash);
    return true;
}

bool ConstantTimeEqual(const std::string& left, const std::string& right) {
    size_t max_size = (std::max)(left.size(), right.size());
    unsigned char different = static_cast<unsigned char>(left.size() ^ right.size());
    for (size_t i = 0; i < max_size; ++i) {
        unsigned char a = i < left.size() ? static_cast<unsigned char>(left[i]) : 0;
        unsigned char b = i < right.size() ? static_cast<unsigned char>(right[i]) : 0;
        different |= static_cast<unsigned char>(a ^ b);
    }
    return different == 0;
}

bool WriteCredentials(const Credentials& credentials, std::string& error) {
    JsonValue root = JsonValue::MakeObject();
    root.At("username") = JsonValue::MakeString(credentials.username);
    root.At("password_salt") = JsonValue::MakeString(credentials.salt_hex);
    root.At("password_hash") = JsonValue::MakeString(credentials.hash_hex);
    root.At("iterations") = JsonValue::MakeNumber(static_cast<double>(credentials.iterations));

    std::error_code ec;
    std::filesystem::create_directories(ConfigPath().parent_path(), ec);
    if (ec) {
        error = "Не удалось создать каталог настроек авторизации: " + ec.message();
        return false;
    }
    const auto temporary = ConfigPath().string() + ".tmp";
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        if (!out) {
            error = "Не удалось сохранить настройки авторизации";
            return false;
        }
        out << ToJson(root, 2) << "\n";
        if (!out) {
            error = "Ошибка записи настроек авторизации";
            return false;
        }
    }
    std::filesystem::remove(ConfigPath(), ec);
    ec.clear();
    std::filesystem::rename(temporary, ConfigPath(), ec);
    if (ec) {
        error = "Не удалось применить настройки авторизации: " + ec.message();
        std::filesystem::remove(temporary, ec);
        return false;
    }
    return true;
}

bool LoadCredentials(Credentials& credentials, std::string& error) {
    std::ifstream in(ConfigPath(), std::ios::binary);
    if (!in) {
        error = "Файл настроек авторизации не найден";
        return false;
    }
    std::ostringstream text;
    text << in.rdbuf();
    JsonParseResult parsed = ParseJson(text.str());
    if (!parsed.ok || !parsed.value.IsObject()) {
        error = "Файл настроек авторизации повреждён";
        return false;
    }
    credentials.username = JsonString(parsed.value, "username", "");
    credentials.salt_hex = JsonString(parsed.value, "password_salt", "");
    credentials.hash_hex = JsonString(parsed.value, "password_hash", "");
    credentials.iterations = static_cast<unsigned long long>(
        JsonInt(parsed.value, "iterations", static_cast<int>(kPbkdf2Iterations)));
    std::vector<unsigned char> salt;
    std::vector<unsigned char> hash;
    if (credentials.username.empty() || credentials.iterations < 10000 ||
        !Unhex(credentials.salt_hex, salt) || !Unhex(credentials.hash_hex, hash) || hash.size() != 32) {
        error = "Файл настроек авторизации содержит некорректные данные";
        return false;
    }
    return true;
}

void RemoveExpiredSessions() {
    const auto now = std::chrono::steady_clock::now();
    for (auto it = g_sessions.begin(); it != g_sessions.end();) {
        if (it->second.expires <= now) it = g_sessions.erase(it);
        else ++it;
    }
}

std::string NewSessionToken() {
    std::vector<unsigned char> bytes;
    return RandomBytes(32, bytes) ? Hex(bytes) : std::string();
}

bool UsernameIsValid(const std::string& username) {
    if (username.size() < 3 || username.size() > 64) return false;
    return std::none_of(username.begin(), username.end(), [](unsigned char c) {
        return std::iscntrl(c) || std::isspace(c);
    });
}

}  // namespace

bool EnsureAuthConfig(std::string& error) {
    std::lock_guard<std::mutex> lock(g_auth_mutex);
    if (std::filesystem::exists(ConfigPath())) {
        Credentials credentials;
        return LoadCredentials(credentials, error);
    }
    Credentials initial;
    initial.username = kInitialUsername;
    initial.salt_hex = kInitialSalt;
    initial.hash_hex = kInitialHash;
    return WriteCredentials(initial, error);
}

std::string AuthUsername() {
    std::lock_guard<std::mutex> lock(g_auth_mutex);
    Credentials credentials;
    std::string error;
    return LoadCredentials(credentials, error) ? credentials.username : std::string();
}

bool VerifyCredentials(const std::string& username, const std::string& password) {
    std::lock_guard<std::mutex> lock(g_auth_mutex);
    Credentials credentials;
    std::string error;
    if (!LoadCredentials(credentials, error)) return false;
    std::string actual_hash;
    if (!DerivePasswordHash(password, credentials.salt_hex, credentials.iterations, actual_hash)) return false;
    return ConstantTimeEqual(username, credentials.username) &&
           ConstantTimeEqual(actual_hash, credentials.hash_hex);
}

AuthResult CreateAuthSession(const std::string& username, const std::string& password) {
    if (!VerifyCredentials(username, password)) return {false, "", "Неверный логин или пароль"};
    std::lock_guard<std::mutex> lock(g_auth_mutex);
    RemoveExpiredSessions();
    std::string token = NewSessionToken();
    if (token.empty()) return {false, "", "Не удалось создать безопасную сессию"};
    g_sessions[token] = {username, std::chrono::steady_clock::now() + std::chrono::hours(kSessionHours)};
    return {true, token, ""};
}

bool ValidateAuthSession(const std::string& token, std::string& username) {
    if (token.empty()) return false;
    std::lock_guard<std::mutex> lock(g_auth_mutex);
    RemoveExpiredSessions();
    auto it = g_sessions.find(token);
    if (it == g_sessions.end()) return false;
    username = it->second.username;
    it->second.expires = std::chrono::steady_clock::now() + std::chrono::hours(kSessionHours);
    return true;
}

void DestroyAuthSession(const std::string& token) {
    std::lock_guard<std::mutex> lock(g_auth_mutex);
    g_sessions.erase(token);
}

AuthResult ChangeAuthCredentials(const std::string& session_token,
                                 const std::string& current_password,
                                 const std::string& new_username,
                                 const std::string& new_password) {
    std::string session_username;
    if (!ValidateAuthSession(session_token, session_username))
        return {false, "", "Сессия истекла. Войдите снова"};
    if (!UsernameIsValid(new_username))
        return {false, "", "Логин: от 3 до 64 символов, без пробелов"};
    if (new_password.size() < 8 || new_password.size() > 128)
        return {false, "", "Пароль должен содержать от 8 до 128 символов"};
    if (!VerifyCredentials(session_username, current_password))
        return {false, "", "Текущий пароль указан неверно"};

    std::vector<unsigned char> salt;
    if (!RandomBytes(16, salt)) return {false, "", "Не удалось создать соль пароля"};
    Credentials updated;
    updated.username = new_username;
    updated.salt_hex = Hex(salt);
    if (!DerivePasswordHash(new_password, updated.salt_hex, updated.iterations, updated.hash_hex))
        return {false, "", "Не удалось безопасно обработать пароль"};

    std::lock_guard<std::mutex> lock(g_auth_mutex);
    std::string error;
    if (!WriteCredentials(updated, error)) return {false, "", error};
    g_sessions.clear();
    std::string new_token = NewSessionToken();
    if (new_token.empty()) return {false, "", "Данные сохранены, но не удалось продлить сессию"};
    g_sessions[new_token] = {new_username,
        std::chrono::steady_clock::now() + std::chrono::hours(kSessionHours)};
    return {true, new_token, ""};
}

}  // namespace timetable
