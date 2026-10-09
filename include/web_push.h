#pragma once

#include <string>
#include <vector>

// Web Push notification sending using VAPID authentication
// Implements the Web Push Protocol: https://datatracker.ietf.org/doc/html/rfc8030

struct WebPushKeys {
    std::string public_key;   // Base64url encoded VAPID public key (P-256)
    std::string private_key;  // Base64url encoded VAPID private key
};

struct WebPushSubscription {
    std::string endpoint;
    std::string p256dh;  // Client public key (base64url)
    std::string auth;    // Client auth secret (base64url)
};

struct WebPushResult {
    bool success;
    int http_code;
    std::string error;
};

// Generate VAPID key pair (ECDSA P-256)
WebPushKeys GenerateVapidKeys();

// Send Web Push notification
// payload: UTF-8 JSON string to send
// subscription: Push subscription details
// vapid_private_key: Base64url encoded private key
// vapid_public_key: Base64url encoded public key
// subject: mailto: or https: URL identifying the application
WebPushResult SendWebPush(
    const std::string& payload,
    const WebPushSubscription& subscription,
    const std::string& vapid_private_key,
    const std::string& vapid_public_key,
    const std::string& subject
);

// Base64url encoding/decoding helpers
std::string Base64UrlEncode(const std::vector<unsigned char>& data);
std::vector<unsigned char> Base64UrlDecode(const std::string& encoded);
