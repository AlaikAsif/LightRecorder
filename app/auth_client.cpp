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

static std::wstring utf8_to_wstring(const std::string& s) {
    if (s.empty()) return std::wstring();
    int needed = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), NULL, 0);
    if (needed <= 0) return std::wstring();
    std::wstring w;
    w.resize(needed);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], needed);
    return w;
}

AuthClient::AuthClient(const std::string& serverUrl) : serverUrl(serverUrl) {}

bool AuthClient::login(const std::string& username, const std::string& password) {
    std::string body = "{\"username\":\"" + username + "\",\"password\":\"" + password + "\"}";
    std::string resp;
    if (!sendPost("/v1/auth/login", body, std::string(), resp)) return false;

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
    std::string enc = encryptDPAPI(recToken);
    if (enc.empty()) return;
    cachedRecToken = enc;

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

// Simplified WinHTTP POST implementation using manual URL parsing to avoid WinHttpCrackUrl
bool AuthClient::sendPost(const std::string& path, const std::string& body, const std::string& authHeader, std::string& outResponse) {
    std::string fullUrl = serverUrl + path;

    // parse scheme://host:port/path
    std::string scheme = "http";
    size_t pos = fullUrl.find("://");
    size_t idx = 0;
    if (pos != std::string::npos) {
        scheme = fullUrl.substr(0, pos);
        idx = pos + 3;
    }
    size_t slash = fullUrl.find('/', idx);
    std::string hostport = (slash == std::string::npos) ? fullUrl.substr(idx) : fullUrl.substr(idx, slash - idx);
    std::string pathPart = (slash == std::string::npos) ? std::string("/") : fullUrl.substr(slash);

    std::string host = hostport;
    int port = (scheme == "https") ? 443 : 80;
    size_t colon = hostport.find(':');
    if (colon != std::string::npos) {
        host = hostport.substr(0, colon);
        try { port = std::stoi(hostport.substr(colon + 1)); } catch(...) { port = (scheme == "https") ? 443 : 80; }
    }

    HINTERNET hSession = WinHttpOpen(L"UltraLightRecorder/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return false;

    HINTERNET hConnect = WinHttpConnect(hSession, utf8_to_wstring(host).c_str(), port, 0);
    if (!hConnect) { WinHttpCloseHandle(hSession); return false; }

    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"POST", utf8_to_wstring(pathPart).c_str(), NULL, WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES, (scheme == "https") ? WINHTTP_FLAG_SECURE : 0);
    if (!hRequest) { WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); return false; }

    std::wstring headers = L"Content-Type: application/json";
    if (!authHeader.empty()) headers += L"\r\n" + utf8_to_wstring(authHeader);

    BOOL send = WinHttpSendRequest(hRequest, headers.c_str(), (DWORD)-1, (LPVOID)body.c_str(), (DWORD)body.size(), (DWORD)body.size(), 0);
    if (!send) { WinHttpCloseHandle(hRequest); WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); return false; }

    if (!WinHttpReceiveResponse(hRequest, NULL)) { WinHttpCloseHandle(hRequest); WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); return false; }

    outResponse.clear();
    while (true) {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(hRequest, &available)) break;
        if (available == 0) break;
        std::vector<char> buffer(available + 1);
        DWORD downloaded = 0;
        if (WinHttpReadData(hRequest, buffer.data(), available, &downloaded) && downloaded > 0) {
            outResponse.append(buffer.data(), downloaded);
        } else break;
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return true;
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

std::string AuthClient::extractJsonString(const std::string& json, const std::string& key) const {
    // Very small ad-hoc extractor: "key":"value"
    std::string needle = "\"" + key + "\"\s*:\s*\""; // try simple form
    size_t pos = json.find("\"" + key + "\":\"");
    if (pos == std::string::npos) return std::string();
    pos = pos + key.length() + 4; // skip "key":"
    size_t end = json.find('"', pos);
    if (end == std::string::npos) return std::string();
    return json.substr(pos, end - pos);
}

std::string AuthClient::base64Encode(const std::string& data) const {
    DWORD len = 0;
    if (!CryptBinaryToStringA((const BYTE*)data.data(), (DWORD)data.size(), CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, NULL, &len)) return std::string();
    std::string out;
    out.resize(len);
    if (!CryptBinaryToStringA((const BYTE*)data.data(), (DWORD)data.size(), CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, &out[0], &len)) return std::string();
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