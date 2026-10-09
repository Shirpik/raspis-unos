#pragma once

#include <string>

// Структура для хранения подписки на Web Push
struct WebPushSubscription {
    std::string endpoint;
    std::string p256dh;  // Ключ клиента (base64url)
    std::string auth;    // Секрет клиента (base64url)
};

// Результат отправки Web Push уведомления
struct WebPushResult {
    bool success;
    int http_code;
    std::string error_message;
};

// Пара VAPID ключей (публичный и приватный)
struct WebPushKeys {
    std::string public_key;   // Base64url encoded
    std::string private_key;  // Base64url encoded
};

// Генерирует пару VAPID ключей (P-256 EC keys)
WebPushKeys GenerateVapidKeys();

// Отправляет Web Push уведомление
// payload: JSON строка с данными уведомления
// subscription: объект подписки клиента
// vapid_private_key: приватный VAPID ключ (base64url)
// vapid_public_key: публичный VAPID ключ (base64url)
// subject: email или URL отправителя (например, "mailto:admin@example.com")
WebPushResult SendWebPush(
    const std::string& payload,
    const WebPushSubscription& subscription,
    const std::string& vapid_private_key,
    const std::string& vapid_public_key,
    const std::string& subject
);
