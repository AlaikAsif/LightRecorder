#include "auth_client.h"
#include <iostream>
#include <string>
#include <windows.h>
#include <wincrypt.h>
#include <winhttp.h>
#include <vector>
#include <fstream>
#include <sstream>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "crypt32.lib")

class AuthClient {
public:
    AuthClient(const std::string& serverUrl);
    bool login(const std::string& username, const std::string& password);
    bool loginWithProductKey(const std::string& productKey);
    bool validateEntitlement();
    std::string getAccessToken() const;
    std::string getRefreshToken() const;
    std::string getRecToken() const;

private:
    std::string serverUrl;
    std::string accessToken;
    std::string refreshToken;
    std::string recToken;
    std::string cachedRecToken;

    bool sendLoginRequest(const std::string& username, const std::string& password);
    void cacheRecToken(const std::string& token);
    bool sendPost(const std::string& path, const std::string& body, const std::string& authHeader, std::string& outResponse);
    std::string encryptDPAPI(const std::string& token);
    std::string decryptDPAPI(const std::string& token);
    std::string getHWID() const;
    std::string extractJsonString(const std::string& json, const std::string& key) const;
    std::string base64Encode(const std::string& data) const;
    std::string base64Decode(const std::string& data) const;
};

AuthClient::AuthClient(const std::string& serverUrl) : serverUrl(serverUrl) {}

bool AuthClient::login(const std::string& username, const std::string& password) {
    std::string body = "{\"username\":\"" + username + "\",\"password\":\"" + password + "\"}";
    std::string resp;
    if (!sendPost("/v1/auth/login", body, std::string(), resp)) return false;

    // Very small JSON extraction helper (expects "access_token":"..." etc.)
    accessToken = extractJsonString(resp, "access_token");
    refreshToken = extractJsonString(resp, "refresh_token");
    std::string recToken = extractJsonString(resp, "rec_token");
    if (!recToken.empty()) {
        cacheRecToken(recToken);
    }
    return !accessToken.empty();
}

bool AuthClient::loginWithProductKey(const std::string& productKey) {
    std::string body = "{\"product_key\":\"" + productKey + "\"}";
    std::string resp;
    if (!sendPost("/v1/auth/product_key", body, std::string(), resp)) return false;

    accessToken = extractJsonString(resp, "access_token");
    refreshToken = extractJsonString(resp, "refresh_token");
    std::string recToken = extractJsonString(resp, "rec_token");
    if (!recToken.empty()) cacheRecToken(recToken);
    return !accessToken.empty();
}

bool AuthClient::validateEntitlement() {
    // Prefer in-memory accessToken; if absent, try to use cached rec token
    if (accessToken.empty()) {
        std::string cached = getCachedRecToken();
        return !cached.empty();
    }

    std::string body = "{}";
    std::string authHeader = "Authorization: Bearer " + accessToken;
    std::string resp;
    if (!sendPost("/v1/entitlement/validate", body, authHeader, resp)) return false;
    std::string recToken = extractJsonString(resp, "rec_token");
    if (!recToken.empty()) {
        cacheRecToken(recToken);
        return true;
    }
    return false;
}

std::string AuthClient::getAccessToken() const { return accessToken; }
std::string AuthClient::getRefreshToken() const { return refreshToken; }

void AuthClient::cacheRecToken(const std::string& recToken) {
    // Encrypt and persist to disk
    std::string enc = encryptDPAPI(recToken);
    if (enc.empty()) return;
    cachedRecToken = enc;

    // Ensure folder in %LOCALAPPDATA%\UltraLightRecorder
    char* localAppData = nullptr;
    size_t len = 0;
    _dupenv_s(&localAppData, &len, "LOCALAPPDATA");
    if (!localAppData) return;

    std::string dir = std::string(localAppData) + "\\UltraLightRecorder";
    CreateDirectoryA(dir.c_str(), NULL);
    std::string path = dir + "\\rec_token.bin";

    std::ofstream out(path, std::ios::binary);
    if (out) {
        out << enc;
        out.close();
    }
    free(localAppData);
}

std::string AuthClient::getCachedRecToken() const {
    if (!cachedRecToken.empty()) {
        // return decrypted
        std::string dec = const_cast<AuthClient*>(this)->decryptDPAPI(cachedRecToken);
        return dec;
    }

    char* localAppData = nullptr;
    size_t len = 0;
    _dupenv_s(&localAppData, &len, "LOCALAPPDATA");
    if (!localAppData) return std::string();
    std::string path = std::string(localAppData) + "\\UltraLightRecorder\\rec_token.bin";
    free(localAppData);

    std::ifstream in(path, std::ios::binary);
    if (!in) return std::string();
    std::ostringstream ss;
    ss << in.rdbuf();
    std::string enc = ss.str();
    std::string dec = const_cast<AuthClient*>(this)->decryptDPAPI(enc);
    return dec;
}

// Minimal WinHTTP POST implementation using WinHttpCrackUrl
bool AuthClient::sendPost(const std::string& path, const std::string& body, const std::string& authHeader, std::string& outResponse) {
    URL_COMPONENTSA urlComp;
    ZeroMemory(&urlComp, sizeof(urlComp));
    urlComp.dwStructSize = sizeof(urlComp);

    // Prepare buffers for host and path
    char host[256] = {0};
    char urlPath[2048] = {0};
    urlComp.lpszHostName = host;
    urlComp.dwHostNameLength = _countof(host);
    urlComp.lpszUrlPath = urlPath;
    urlComp.dwUrlPathLength = _countof(urlPath);

    std::string fullUrl = serverUrl + path;
    if (!WinHttpCrackUrl(fullUrl.c_str(), (DWORD)fullUrl.length(), 0, &urlComp)) {
        return false;
    }

    bool result = false;
    HINTERNET hSession = WinHttpOpen(L"UltraLightRecorder/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return false;

    std::wstring hostW;
    int reqPort = urlComp.nPort;
    {
        int needed = MultiByteToWideChar(CP_UTF8, 0, urlComp.lpszHostName, urlComp.dwHostNameLength, NULL, 0);
        hostW.resize(needed);
        MultiByteToWideChar(CP_UTF8, 0, urlComp.lpszHostName, urlComp.dwHostNameLength, &hostW[0], needed);
    }

    HINTERNET hConnect = WinHttpConnect(hSession, hostW.c_str(), reqPort, 0);
    if (!hConnect) {
        WinHttpCloseHandle(hSession);
        return false;
    }

    std::wstring pathW;
    {
        int needed = MultiByteToWideChar(CP_UTF8, 0, urlComp.lpszUrlPath, urlComp.dwUrlPathLength, NULL, 0);
        pathW.resize(needed);
        MultiByteToWideChar(CP_UTF8, 0, urlComp.lpszUrlPath, urlComp.dwUrlPathLength, &pathW[0], needed);
    }

    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"POST", pathW.c_str(), NULL, WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES, (urlComp.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0);
    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }

    std::wstring headers = L"Content-Type: application/json";
    if (!authHeader.empty()) {
        // convert authHeader to wide and append
        int needed = MultiByteToWideChar(CP_UTF8, 0, authHeader.c_str(), (int)authHeader.size(), NULL, 0);
        std::wstring authW;
        authW.resize(needed);
        MultiByteToWideChar(CP_UTF8, 0, authHeader.c_str(), (int)authHeader.size(), &authW[0], needed);
        headers += L"\r\n" + authW;
    }

    std::wstring bodyW;
    {
        int needed = MultiByteToWideChar(CP_UTF8, 0, body.c_str(), (int)body.size(), NULL, 0);
        bodyW.resize(needed);
        MultiByteToWideChar(CP_UTF8, 0, body.c_str(), (int)body.size(), &bodyW[0], needed);
    }

    BOOL send = WinHttpSendRequest(hRequest, headers.c_str(), (DWORD)-1L, (LPVOID)body.c_str(), (DWORD)body.size(), (DWORD)body.size(), 0);
    if (!send) {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }

    if (!WinHttpReceiveResponse(hRequest, NULL)) {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }

    // Read response
    DWORD dwSize = 0;
    do {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(hRequest, &available)) break;
        if (available == 0) break;
        std::vector<char> buffer(available + 1);
        DWORD downloaded = 0;
        if (WinHttpReadData(hRequest, buffer.data(), available, &downloaded) && downloaded > 0) {
            outResponse.append(buffer.data(), downloaded);
        } else break;
    } while (dwSize != 0);

    result = true;
    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return result;
}

// DPAPI encryption using HWID as optional entropy
std::string AuthClient::encryptDPAPI(const std::string& plain) {
    std::string hwid = getHWID();

    DATA_BLOB inBlob;
    inBlob.pbData = (BYTE*)plain.data();
    inBlob.cbData = (DWORD)plain.size();

    DATA_BLOB entropyBlob;
    entropyBlob.pbData = (BYTE*)hwid.data();
    entropyBlob.cbData = (DWORD)hwid.size();

    DATA_BLOB outBlob;
    if (!CryptProtectData(&inBlob, L"rec_token", &entropyBlob, NULL, NULL, 0, &outBlob)) {
        return std::string();
    }

    // base64 encode
    std::string b64 = base64Encode(std::string((char*)outBlob.pbData, outBlob.cbData));
    LocalFree(outBlob.pbData);
    return b64;
}

std::string AuthClient::decryptDPAPI(const std::string& cipherBase64) {
    std::string hwid = getHWID();
    std::string cipher = base64Decode(cipherBase64);
    if (cipher.empty()) return std::string();

    DATA_BLOB inBlob;
    inBlob.pbData = (BYTE*)cipher.data();
    inBlob.cbData = (DWORD)cipher.size();

    DATA_BLOB entropyBlob;
    entropyBlob.pbData = (BYTE*)hwid.data();
    entropyBlob.cbData = (DWORD)hwid.size();

    DATA_BLOB outBlob;
    if (!CryptUnprotectData(&inBlob, NULL, &entropyBlob, NULL, NULL, 0, &outBlob)) {
        return std::string();
    }

    std::string out((char*)outBlob.pbData, outBlob.cbData);
    LocalFree(outBlob.pbData);
    return out;
}

std::string AuthClient::getHWID() const {
    // Simple HWID: volume serial of system drive + computer name
    DWORD serial = 0;
    char sysRoot[MAX_PATH] = "C:\\";
    GetVolumeInformationA(sysRoot, NULL, 0, &serial, NULL, NULL, NULL, 0);

    char name[256] = {0};
    DWORD size = _countof(name);
    GetComputerNameA(name, &size);

    std::ostringstream ss;
    ss << std::hex << serial << ":" << name;
    return ss.str();
}

// Very small JSON extractor (not a substitute for a real JSON parser)
std::string AuthClient::extractJsonString(const std::string& json, const std::string& key) const {
    std::string pattern = "\"" + key + "\"\s*:\s*\"";
    size_t pos = json.find(pattern);
    if (pos == std::string::npos) return std::string();
    pos += pattern.length();
    size_t end = json.find('"', pos);
    if (end == std::string::npos) return std::string();
    return json.substr(pos, end - pos);
}

// Base64 helpers using Windows APIs
std::string AuthClient::base64Encode(const std::string& data) const {
    DWORD len = 0;
    if (!CryptBinaryToStringA((const BYTE*)data.data(), (DWORD)data.size(), CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, NULL, &len)) return std::string();
    std::string out;
    out.resize(len);
    if (!CryptBinaryToStringA((const BYTE*)data.data(), (DWORD)data.size(), CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, &out[0], &len)) return std::string();
    // remove any trailing null produced by API
    if (!out.empty() && out.back() == '\0') out.pop_back();
    return out;
}

std::string AuthClient::base64Decode(const std::string& data) const {
    DWORD len = 0;
    if (!CryptStringToBinaryA(data.c_str(), (DWORD)data.size(), CRYPT_STRING_BASE64, NULL, &len, NULL, NULL)) return std::string();
    std::string out;
    out.resize(len);
    if (!CryptStringToBinaryA(data.c_str(), (DWORD)data.size(), CRYPT_STRING_BASE64, (BYTE*)&out[0], &len, NULL, NULL)) return std::string();
    return out;
}