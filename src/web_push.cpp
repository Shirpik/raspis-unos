#include "web_push.h"
#include <openssl/ec.h>
#include <openssl/ecdh.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/sha.h>
#include <openssl/hmac.h>
#include <openssl/kdf.h>
#include <cstring>
#include <sstream>
#include <iomanip>
#include <ctime>
#include <algorithm>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#endif

// Base64url encoding (RFC 4648)
static const char base64_chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

std::string Base64UrlEncode(const std::vector<unsigned char>& data) {
    std::string result;
    result.reserve((data.size() * 4) / 3 + 3);

    size_t i = 0;
    while (i + 2 < data.size()) {
        result += base64_chars[(data[i] >> 2) & 0x3F];
        result += base64_chars[((data[i] & 0x03) << 4) | ((data[i + 1] >> 4) & 0x0F)];
        result += base64_chars[((data[i + 1] & 0x0F) << 2) | ((data[i + 2] >> 6) & 0x03)];
        result += base64_chars[data[i + 2] & 0x3F];
        i += 3;
    }

    if (i < data.size()) {
        result += base64_chars[(data[i] >> 2) & 0x3F];
        if (i + 1 < data.size()) {
            result += base64_chars[((data[i] & 0x03) << 4) | ((data[i + 1] >> 4) & 0x0F)];
            result += base64_chars[((data[i + 1] & 0x0F) << 2)];
        } else {
            result += base64_chars[((data[i] & 0x03) << 4)];
        }
    }

    return result;
}

std::vector<unsigned char> Base64UrlDecode(const std::string& encoded) {
    std::vector<unsigned char> result;
    std::vector<int> decode_table(256, -1);

    for (int i = 0; i < 64; ++i) {
        decode_table[static_cast<unsigned char>(base64_chars[i])] = i;
    }

    size_t i = 0;
    while (i < encoded.size()) {
        int a = decode_table[static_cast<unsigned char>(encoded[i++])];
        if (i >= encoded.size()) break;
        int b = decode_table[static_cast<unsigned char>(encoded[i++])];
        if (i >= encoded.size()) {
            if (a >= 0 && b >= 0) result.push_back((a << 2) | (b >> 4));
            break;
        }
        int c = decode_table[static_cast<unsigned char>(encoded[i++])];
        if (i >= encoded.size()) {
            if (a >= 0 && b >= 0) result.push_back((a << 2) | (b >> 4));
            if (b >= 0 && c >= 0) result.push_back((b << 4) | (c >> 2));
            break;
        }
        int d = decode_table[static_cast<unsigned char>(encoded[i++])];

        if (a >= 0 && b >= 0) result.push_back((a << 2) | (b >> 4));
        if (b >= 0 && c >= 0) result.push_back((b << 4) | (c >> 2));
        if (c >= 0 && d >= 0) result.push_back((c << 6) | d);
    }

    return result;
}

// Generate VAPID key pair
WebPushKeys GenerateVapidKeys() {
    WebPushKeys keys;

    // Create EC key (P-256 / prime256v1 / secp256r1)
    EC_KEY* ec_key = EC_KEY_new_by_curve_name(NID_X9_62_prime256v1);
    if (!ec_key) return keys;

    if (EC_KEY_generate_key(ec_key) != 1) {
        EC_KEY_free(ec_key);
        return keys;
    }

    // Get public key (uncompressed format: 0x04 + X + Y)
    const EC_POINT* pub_point = EC_KEY_get0_public_key(ec_key);
    const EC_GROUP* group = EC_KEY_get0_group(ec_key);

    unsigned char pub_key_buf[65];
    size_t pub_len = EC_POINT_point2oct(group, pub_point, POINT_CONVERSION_UNCOMPRESSED,
                                         pub_key_buf, sizeof(pub_key_buf), nullptr);

    if (pub_len == 65) {
        std::vector<unsigned char> pub_vec(pub_key_buf, pub_key_buf + pub_len);
        keys.public_key = Base64UrlEncode(pub_vec);
    }

    // Get private key
    const BIGNUM* priv_bn = EC_KEY_get0_private_key(ec_key);
    unsigned char priv_key_buf[32];
    int priv_len = BN_bn2binpad(priv_bn, priv_key_buf, 32);

    if (priv_len == 32) {
        std::vector<unsigned char> priv_vec(priv_key_buf, priv_key_buf + 32);
        keys.private_key = Base64UrlEncode(priv_vec);
    }

    EC_KEY_free(ec_key);
    return keys;
}

// Create VAPID JWT token
static std::string CreateVapidJWT(const std::string& audience,
                                   const std::string& subject,
                                   const std::string& private_key_b64) {
    // JWT header
    std::string header = R"({"typ":"JWT","alg":"ES256"})";
    std::vector<unsigned char> header_vec(header.begin(), header.end());
    std::string header_b64 = Base64UrlEncode(header_vec);

    // JWT payload
    time_t now = time(nullptr);
    time_t exp = now + 43200; // 12 hours

    std::ostringstream payload_stream;
    payload_stream << "{\"aud\":\"" << audience << "\","
                   << "\"exp\":" << exp << ","
                   << "\"sub\":\"" << subject << "\"}";
    std::string payload = payload_stream.str();
    std::vector<unsigned char> payload_vec(payload.begin(), payload.end());
    std::string payload_b64 = Base64UrlEncode(payload_vec);

    // Message to sign
    std::string message = header_b64 + "." + payload_b64;

    // Decode private key
    std::vector<unsigned char> priv_key_bytes = Base64UrlDecode(private_key_b64);
    if (priv_key_bytes.size() != 32) return "";

    // Create EC key from private key
    EC_KEY* ec_key = EC_KEY_new_by_curve_name(NID_X9_62_prime256v1);
    if (!ec_key) return "";

    BIGNUM* priv_bn = BN_bin2bn(priv_key_bytes.data(), 32, nullptr);
    if (!priv_bn) {
        EC_KEY_free(ec_key);
        return "";
    }

    if (EC_KEY_set_private_key(ec_key, priv_bn) != 1) {
        BN_free(priv_bn);
        EC_KEY_free(ec_key);
        return "";
    }

    // Compute public key from private key
    const EC_GROUP* group = EC_KEY_get0_group(ec_key);
    EC_POINT* pub_point = EC_POINT_new(group);
    if (EC_POINT_mul(group, pub_point, priv_bn, nullptr, nullptr, nullptr) != 1) {
        EC_POINT_free(pub_point);
        BN_free(priv_bn);
        EC_KEY_free(ec_key);
        return "";
    }

    EC_KEY_set_public_key(ec_key, pub_point);
    EC_POINT_free(pub_point);
    BN_free(priv_bn);

    // Sign message with ECDSA
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char*>(message.data()), message.size(), hash);

    ECDSA_SIG* signature = ECDSA_do_sign(hash, SHA256_DIGEST_LENGTH, ec_key);
    EC_KEY_free(ec_key);

    if (!signature) return "";

    // Convert ECDSA signature to raw format (r || s)
    const BIGNUM* r = nullptr;
    const BIGNUM* s = nullptr;
    ECDSA_SIG_get0(signature, &r, &s);

    unsigned char r_bytes[32];
    unsigned char s_bytes[32];
    BN_bn2binpad(r, r_bytes, 32);
    BN_bn2binpad(s, s_bytes, 32);

    ECDSA_SIG_free(signature);

    std::vector<unsigned char> sig_vec(64);
    std::memcpy(sig_vec.data(), r_bytes, 32);
    std::memcpy(sig_vec.data() + 32, s_bytes, 32);

    std::string sig_b64 = Base64UrlEncode(sig_vec);

    return message + "." + sig_b64;
}

// HKDF-SHA256 implementation
static bool HKDF_SHA256(const unsigned char* salt, size_t salt_len,
                        const unsigned char* ikm, size_t ikm_len,
                        const unsigned char* info, size_t info_len,
                        unsigned char* okm, size_t okm_len) {
    // Extract
    unsigned char prk[SHA256_DIGEST_LENGTH];
    unsigned int prk_len;

    if (!HMAC(EVP_sha256(), salt, salt_len, ikm, ikm_len, prk, &prk_len)) {
        return false;
    }

    // Expand
    unsigned char prev[SHA256_DIGEST_LENGTH];
    size_t written = 0;
    unsigned char counter = 1;

    while (written < okm_len) {
        HMAC_CTX* ctx = HMAC_CTX_new();
        if (!ctx) return false;

        if (!HMAC_Init_ex(ctx, prk, prk_len, EVP_sha256(), nullptr)) {
            HMAC_CTX_free(ctx);
            return false;
        }

        if (counter > 1) {
            HMAC_Update(ctx, prev, SHA256_DIGEST_LENGTH);
        }

        HMAC_Update(ctx, info, info_len);
        HMAC_Update(ctx, &counter, 1);

        unsigned int out_len;
        HMAC_Final(ctx, prev, &out_len);
        HMAC_CTX_free(ctx);

        size_t to_copy = std::min<size_t>(out_len, okm_len - written);
        std::memcpy(okm + written, prev, to_copy);
        written += to_copy;
        counter++;
    }

    return true;
}

// AES-128-GCM encryption
static std::vector<unsigned char> EncryptAESGCM(const std::vector<unsigned char>& plaintext,
                                                 const unsigned char* key,
                                                 const unsigned char* nonce,
                                                 std::vector<unsigned char>& tag) {
    std::vector<unsigned char> ciphertext(plaintext.size());

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return {};

    if (EVP_EncryptInit_ex(ctx, EVP_aes_128_gcm(), nullptr, nullptr, nullptr) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return {};
    }

    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, 12, nullptr) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return {};
    }

    if (EVP_EncryptInit_ex(ctx, nullptr, nullptr, key, nonce) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return {};
    }

    int len;
    if (EVP_EncryptUpdate(ctx, ciphertext.data(), &len, plaintext.data(), plaintext.size()) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return {};
    }

    int final_len;
    if (EVP_EncryptFinal_ex(ctx, ciphertext.data() + len, &final_len) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return {};
    }

    tag.resize(16);
    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, 16, tag.data()) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return {};
    }

    EVP_CIPHER_CTX_free(ctx);
    ciphertext.resize(len + final_len);
    return ciphertext;
}

// Simple HTTP POST using sockets
static WebPushResult SendHTTPPost(const std::string& url,
                                   const std::string& body,
                                   const std::string& content_encoding,
                                   const std::string& authorization,
                                   int ttl) {
    WebPushResult result;
    result.success = false;
    result.http_code = 0;

    // Parse URL
    std::string protocol, host, path;
    size_t proto_end = url.find("://");
    if (proto_end == std::string::npos) {
        result.error = "Invalid URL";
        return result;
    }

    protocol = url.substr(0, proto_end);
    size_t host_start = proto_end + 3;
    size_t path_start = url.find('/', host_start);

    if (path_start == std::string::npos) {
        host = url.substr(host_start);
        path = "/";
    } else {
        host = url.substr(host_start, path_start - host_start);
        path = url.substr(path_start);
    }

    // Extract port
    std::string hostname = host;
    std::string port = (protocol == "https") ? "443" : "80";
    size_t colon_pos = host.find(':');
    if (colon_pos != std::string::npos) {
        hostname = host.substr(0, colon_pos);
        port = host.substr(colon_pos + 1);
    }

#ifdef _WIN32
    WSADATA wsa_data;
    WSAStartup(MAKEWORD(2, 2), &wsa_data);
#endif

    // Resolve hostname
    struct addrinfo hints = {};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    struct addrinfo* addr_result = nullptr;
    if (getaddrinfo(hostname.c_str(), port.c_str(), &hints, &addr_result) != 0) {
        result.error = "Failed to resolve hostname";
#ifdef _WIN32
        WSACleanup();
#endif
        return result;
    }

    // Create socket
    int sock = socket(addr_result->ai_family, addr_result->ai_socktype, addr_result->ai_protocol);
    if (sock < 0) {
        result.error = "Failed to create socket";
        freeaddrinfo(addr_result);
#ifdef _WIN32
        WSACleanup();
#endif
        return result;
    }

    // Connect
    if (connect(sock, addr_result->ai_addr, addr_result->ai_addrlen) < 0) {
        result.error = "Failed to connect";
        freeaddrinfo(addr_result);
#ifdef _WIN32
        closesocket(sock);
        WSACleanup();
#else
        close(sock);
#endif
        return result;
    }

    freeaddrinfo(addr_result);

    // Build HTTP request
    std::ostringstream request;
    request << "POST " << path << " HTTP/1.1\r\n";
    request << "Host: " << host << "\r\n";
    request << "Content-Type: application/octet-stream\r\n";
    request << "Content-Length: " << body.size() << "\r\n";
    request << "Content-Encoding: " << content_encoding << "\r\n";
    request << "TTL: " << ttl << "\r\n";
    request << "Authorization: " << authorization << "\r\n";
    request << "Connection: close\r\n";
    request << "\r\n";
    request << body;

    std::string request_str = request.str();

    // Send request
    ssize_t sent = send(sock, request_str.c_str(), request_str.size(), 0);
    if (sent < 0) {
        result.error = "Failed to send request";
#ifdef _WIN32
        closesocket(sock);
        WSACleanup();
#else
        close(sock);
#endif
        return result;
    }

    // Read response
    char buffer[4096];
    std::string response;
    ssize_t received;

    while ((received = recv(sock, buffer, sizeof(buffer) - 1, 0)) > 0) {
        buffer[received] = '\0';
        response += buffer;
    }

#ifdef _WIN32
    closesocket(sock);
    WSACleanup();
#else
    close(sock);
#endif

    // Parse HTTP status code
    size_t status_pos = response.find("HTTP/");
    if (status_pos != std::string::npos) {
        size_t code_start = response.find(' ', status_pos) + 1;
        size_t code_end = response.find(' ', code_start);
        std::string code_str = response.substr(code_start, code_end - code_start);
        result.http_code = std::stoi(code_str);

        if (result.http_code == 201) {
            result.success = true;
        } else if (result.http_code == 410) {
            result.error = "Subscription expired";
        } else {
            result.error = "HTTP " + code_str;
        }
    } else {
        result.error = "Invalid HTTP response";
    }

    return result;
}

// Send Web Push notification
WebPushResult SendWebPush(const std::string& payload,
                          const WebPushSubscription& subscription,
                          const std::string& vapid_private_key,
                          const std::string& vapid_public_key,
                          const std::string& subject) {
    WebPushResult result;
    result.success = false;

    // Extract audience (origin) from endpoint
    size_t proto_end = subscription.endpoint.find("://");
    if (proto_end == std::string::npos) {
        result.error = "Invalid endpoint URL";
        return result;
    }

    size_t host_start = proto_end + 3;
    size_t host_end = subscription.endpoint.find('/', host_start);
    std::string audience = subscription.endpoint.substr(0, host_end);

    // Create VAPID JWT
    std::string jwt = CreateVapidJWT(audience, subject, vapid_private_key);
    if (jwt.empty()) {
        result.error = "Failed to create JWT";
        return result;
    }

    std::string auth_header = "vapid t=" + jwt + ", k=" + vapid_public_key;

    // Decode client keys
    std::vector<unsigned char> client_public = Base64UrlDecode(subscription.p256dh);
    std::vector<unsigned char> auth_secret = Base64UrlDecode(subscription.auth);

    if (client_public.size() != 65 || auth_secret.size() != 16) {
        result.error = "Invalid subscription keys";
        return result;
    }

    // Generate local key pair for ECDH
    EC_KEY* local_key = EC_KEY_new_by_curve_name(NID_X9_62_prime256v1);
    if (!local_key) {
        result.error = "Failed to create local key";
        return result;
    }

    if (EC_KEY_generate_key(local_key) != 1) {
        EC_KEY_free(local_key);
        result.error = "Failed to generate local key";
        return result;
    }

    // Get local public key
    const EC_POINT* local_pub_point = EC_KEY_get0_public_key(local_key);
    const EC_GROUP* group = EC_KEY_get0_group(local_key);

    unsigned char local_pub_buf[65];
    size_t local_pub_len = EC_POINT_point2oct(group, local_pub_point, POINT_CONVERSION_UNCOMPRESSED,
                                                local_pub_buf, sizeof(local_pub_buf), nullptr);

    std::vector<unsigned char> local_public_key(local_pub_buf, local_pub_buf + local_pub_len);

    // Parse client public key
    EC_POINT* client_pub_point = EC_POINT_new(group);
    if (EC_POINT_oct2point(group, client_pub_point, client_public.data(), client_public.size(), nullptr) != 1) {
        EC_POINT_free(client_pub_point);
        EC_KEY_free(local_key);
        result.error = "Invalid client public key";
        return result;
    }

    // Compute shared secret using ECDH
    unsigned char shared_secret[32];
    int shared_len = ECDH_compute_key(shared_secret, 32, client_pub_point, local_key, nullptr);
    EC_POINT_free(client_pub_point);
    EC_KEY_free(local_key);

    if (shared_len != 32) {
        result.error = "ECDH failed";
        return result;
    }

    // Derive encryption key using HKDF
    // ikm = auth_secret || shared_secret
    std::vector<unsigned char> ikm;
    ikm.insert(ikm.end(), auth_secret.begin(), auth_secret.end());
    ikm.insert(ikm.end(), shared_secret, shared_secret + 32);

    const char auth_info[] = "Content-Encoding: auth\0";
    unsigned char prk[32];
    if (!HKDF_SHA256(auth_secret.data(), auth_secret.size(), ikm.data(), ikm.size(),
                     reinterpret_cast<const unsigned char*>(auth_info), sizeof(auth_info) - 1,
                     prk, 32)) {
        result.error = "Key derivation failed";
        return result;
    }

    // Build context
    std::vector<unsigned char> context;
    context.push_back(0x00); // label

    // Client public key length (2 bytes, big-endian)
    context.push_back(0x00);
    context.push_back(0x41); // 65 bytes
    context.insert(context.end(), client_public.begin(), client_public.end());

    // Server (local) public key length
    context.push_back(0x00);
    context.push_back(0x41);
    context.insert(context.end(), local_public_key.begin(), local_public_key.end());

    // Derive content encryption key
    const char cek_info_prefix[] = "Content-Encoding: aes128gcm\0";
    std::vector<unsigned char> cek_info;
    cek_info.insert(cek_info.end(), cek_info_prefix, cek_info_prefix + sizeof(cek_info_prefix) - 1);
    cek_info.insert(cek_info.end(), context.begin(), context.end());

    unsigned char cek[16];
    if (!HKDF_SHA256(auth_secret.data(), auth_secret.size(), prk, 32,
                     cek_info.data(), cek_info.size(), cek, 16)) {
        result.error = "CEK derivation failed";
        return result;
    }

    // Derive nonce
    const char nonce_info_prefix[] = "Content-Encoding: nonce\0";
    std::vector<unsigned char> nonce_info;
    nonce_info.insert(nonce_info.end(), nonce_info_prefix, nonce_info_prefix + sizeof(nonce_info_prefix) - 1);
    nonce_info.insert(nonce_info.end(), context.begin(), context.end());

    unsigned char nonce[12];
    if (!HKDF_SHA256(auth_secret.data(), auth_secret.size(), prk, 32,
                     nonce_info.data(), nonce_info.size(), nonce, 12)) {
        result.error = "Nonce derivation failed";
        return result;
    }

    // Prepare plaintext with padding
    std::vector<unsigned char> plaintext(payload.begin(), payload.end());
    plaintext.push_back(0x02); // delimiter

    // Encrypt
    std::vector<unsigned char> tag;
    std::vector<unsigned char> ciphertext = EncryptAESGCM(plaintext, cek, nonce, tag);
    if (ciphertext.empty()) {
        result.error = "Encryption failed";
        return result;
    }

    // Build final body: local_public_key || ciphertext || tag
    std::vector<unsigned char> body;
    body.insert(body.end(), local_public_key.begin(), local_public_key.end());
    body.insert(body.end(), ciphertext.begin(), ciphertext.end());
    body.insert(body.end(), tag.begin(), tag.end());

    std::string body_str(reinterpret_cast<char*>(body.data()), body.size());

    // Send HTTP POST
    return SendHTTPPost(subscription.endpoint, body_str, "aes128gcm", auth_header, 86400);
}

