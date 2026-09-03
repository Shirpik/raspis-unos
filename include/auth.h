#pragma once

#include <string>

namespace timetable {

struct AuthResult {
    bool ok = false;
    std::string value;
    std::string error;
};

// Создаёт файл с безопасно хешированными начальными учётными данными при
// первом запуске. Открытого пароля в файле нет.
bool EnsureAuthConfig(std::string& error);
std::string AuthUsername();
bool VerifyCredentials(const std::string& username, const std::string& password);

AuthResult CreateAuthSession(const std::string& username, const std::string& password);
bool ValidateAuthSession(const std::string& token, std::string& username);
void DestroyAuthSession(const std::string& token);

AuthResult ChangeAuthCredentials(
    const std::string& session_token,
    const std::string& current_password,
    const std::string& new_username,
    const std::string& new_password);

}  // namespace timetable
