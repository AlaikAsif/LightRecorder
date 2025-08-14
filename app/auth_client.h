#ifndef AUTH_CLIENT_H
#define AUTH_CLIENT_H

#include <string>

class AuthClient {
public:
    // serverUrl default points to localhost for development; can be overridden
    AuthClient(const std::string& serverUrl = "http://127.0.0.1:8000");

    // Login with username/password. On success accessToken/refreshToken are populated.
    bool login(const std::string& username, const std::string& password);

    // Alternative login using a single product key (offline or server-backed product key).
    bool loginWithProductKey(const std::string& productKey);

    // After successful login call this to validate entitlement and obtain a rec_token
    // which will be cached (encrypted with DPAPI + HWID binding).
    bool validateEntitlement();

    std::string getAccessToken() const;
    std::string getRefreshToken() const;

    // Cache/restore encrypted rec_token for offline grace
    void cacheRecToken(const std::string& recToken);
    std::string getCachedRecToken() const;

private:
    std::string serverUrl;
    std::string accessToken;
    std::string refreshToken;
    std::string cachedRecToken; // base64'd encrypted blob

    // Low level HTTP helpers using WinHTTP
    bool sendPost(const std::string& path, const std::string& body, const std::string& authHeader, std::string& outResponse);

    // DPAPI helpers for encrypting/decrypting rec_token; uses HWID as optional entropy
    std::string encryptDPAPI(const std::string& plain);
    std::string decryptDPAPI(const std::string& cipherBase64);

    // Simple helpers
    std::string getHWID() const;
    std::string extractJsonString(const std::string& json, const std::string& key) const;
    std::string base64Encode(const std::string& data) const;
    std::string base64Decode(const std::string& data) const;
};

#endif // AUTH_CLIENT_H