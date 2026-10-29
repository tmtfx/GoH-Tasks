#include "GoogleAuth.h"
#include "HaikuHttp.h"
#include "JsonValue.h"

#include <Directory.h>
#include <Entry.h>
#include <FindDirectory.h>
#include <Path.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <random>
#include <sstream>
#include <stdexcept>
#include <vector>

// ============================================================
//  SHA-256 (FIPS 180-4) — used for PKCE code_challenge
// ============================================================
namespace {

static const uint32_t kShaK[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
    0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
    0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
    0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
    0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
    0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

inline uint32_t rotr32(uint32_t x, unsigned n) {
    return (x >> n) | (x << (32 - n));
}

void sha256(const unsigned char* data, size_t len, unsigned char out[32])
{
    uint32_t h[8] = {
        0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
        0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
    };

    size_t bitLen = len * 8;
    size_t padLen = ((len + 8) / 64 + 1) * 64;
    std::vector<unsigned char> msg(padLen, 0);
    memcpy(msg.data(), data, len);
    msg[len] = 0x80;
    for (int i = 0; i < 8; ++i)
        msg[padLen - 1 - i] = static_cast<unsigned char>(bitLen >> (i * 8));

    for (size_t i = 0; i < padLen; i += 64) {
        uint32_t w[64];
        for (int j = 0; j < 16; ++j) {
            w[j] = (static_cast<uint32_t>(msg[i + j*4    ]) << 24)
                 | (static_cast<uint32_t>(msg[i + j*4 + 1]) << 16)
                 | (static_cast<uint32_t>(msg[i + j*4 + 2]) <<  8)
                 |  static_cast<uint32_t>(msg[i + j*4 + 3]);
        }
        for (int j = 16; j < 64; ++j) {
            uint32_t s0 = rotr32(w[j-15], 7) ^ rotr32(w[j-15], 18) ^ (w[j-15] >> 3);
            uint32_t s1 = rotr32(w[j- 2],17) ^ rotr32(w[j- 2], 19) ^ (w[j- 2] >> 10);
            w[j] = w[j-16] + s0 + w[j-7] + s1;
        }

        uint32_t a = h[0], b = h[1], c = h[2], d = h[3];
        uint32_t e = h[4], f = h[5], g = h[6], hh = h[7];

        for (int j = 0; j < 64; ++j) {
            uint32_t S1    = rotr32(e,  6) ^ rotr32(e, 11) ^ rotr32(e, 25);
            uint32_t ch    = (e & f) ^ (~e & g);
            uint32_t temp1 = hh + S1 + ch + kShaK[j] + w[j];
            uint32_t S0    = rotr32(a,  2) ^ rotr32(a, 13) ^ rotr32(a, 22);
            uint32_t maj   = (a & b) ^ (a & c) ^ (b & c);
            uint32_t temp2 = S0 + maj;

            hh = g; g = f; f = e; e = d + temp1;
            d  = c; c = b; b = a; a = temp1 + temp2;
        }

        h[0] += a; h[1] += b; h[2] += c; h[3] += d;
        h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    }

    for (int i = 0; i < 8; ++i) {
        out[i*4    ] = static_cast<unsigned char>(h[i] >> 24);
        out[i*4 + 1] = static_cast<unsigned char>(h[i] >> 16);
        out[i*4 + 2] = static_cast<unsigned char>(h[i] >>  8);
        out[i*4 + 3] = static_cast<unsigned char>(h[i]);
    }
}

} // anonymous namespace

// ============================================================
//  GoogleAuth
// ============================================================

GoogleAuth::GoogleAuth()
    : fTokenExpiry(0), fLock("GoogleAuth")
{
    BPath settingsPath;
    if (find_directory(B_USER_SETTINGS_DIRECTORY, &settingsPath) == B_OK) {
        settingsPath.Append("GoH-Tasks");
        fSettingsDir = settingsPath.Path();
        BDirectory dir;
        if (dir.SetTo(fSettingsDir.c_str()) != B_OK)
            create_directory(fSettingsDir.c_str(), 0700);
    }
}

GoogleAuth::~GoogleAuth() {}

bool GoogleAuth::loadConfig()
{
    BAutolock lock(fLock);
    std::string path = fSettingsDir + "/config.json";
    std::ifstream f(path);
    if (!f.is_open()) return false;
    std::string content((std::istreambuf_iterator<char>(f)),
                         std::istreambuf_iterator<char>());
    try {
        JsonValue j = JsonValue::parse(content);
        fClientId     = j["client_id"].str();
        fClientSecret = j["client_secret"].str();
        return !fClientId.empty();
    } catch (...) {
        return false;
    }
}

bool GoogleAuth::saveConfig(const std::string& clientId, const std::string& clientSecret)
{
    BAutolock lock(fLock);
    fClientId     = clientId;
    fClientSecret = clientSecret;
    std::string path = fSettingsDir + "/config.json";
    std::ofstream f(path);
    if (!f.is_open()) return false;
    f << "{\"client_id\":\""     << JsonValue::escape(clientId)
      << "\",\"client_secret\":\"" << JsonValue::escape(clientSecret) << "\"}";
    return f.good();
}

bool GoogleAuth::loadTokens()
{
    std::string path = fSettingsDir + "/tokens.json";
    std::ifstream f(path);
    if (!f.is_open()) return false;
    std::string content((std::istreambuf_iterator<char>(f)),
                         std::istreambuf_iterator<char>());
    try {
        JsonValue j = JsonValue::parse(content);
        fAccessToken  = j["access_token"].str();
        fRefreshToken = j["refresh_token"].str();
        fTokenExpiry  = static_cast<time_t>(j["expiry"].num());
        return !fRefreshToken.empty();
    } catch (...) {
        return false;
    }
}

bool GoogleAuth::saveTokens()
{
    std::string path = fSettingsDir + "/tokens.json";
    std::ofstream f(path);
    if (!f.is_open()) return false;
    f << "{\"access_token\":\""  << JsonValue::escape(fAccessToken)
      << "\",\"refresh_token\":\"" << JsonValue::escape(fRefreshToken)
      << "\",\"expiry\":"         << static_cast<long long>(fTokenExpiry) << "}";
    return f.good();
}

bool GoogleAuth::isAuthenticated()
{
    BAutolock lock(fLock);
    return loadTokens() && !fRefreshToken.empty();
}

// ---- PKCE helpers ----

std::string GoogleAuth::base64urlEncode(const unsigned char* data, size_t len)
{
    static const char* kChars =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string result;
    result.reserve(((len + 2) / 3) * 4);
    for (size_t i = 0; i < len; i += 3) {
        uint32_t val  = static_cast<uint32_t>(data[i]) << 16;
        if (i + 1 < len) val |= static_cast<uint32_t>(data[i + 1]) << 8;
        if (i + 2 < len) val |= static_cast<uint32_t>(data[i + 2]);
        result += kChars[(val >> 18) & 0x3F];
        result += kChars[(val >> 12) & 0x3F];
        result += (i + 1 < len) ? kChars[(val >> 6) & 0x3F] : '=';
        result += (i + 2 < len) ? kChars[ val       & 0x3F] : '=';
    }
    for (char& c : result) {
        if      (c == '+') c = '-';
        else if (c == '/') c = '_';
    }
    while (!result.empty() && result.back() == '=')
        result.pop_back();
    return result;
}

std::string GoogleAuth::generateVerifier()
{
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<int> dist(0, 255);
    unsigned char bytes[64];
    for (int i = 0; i < 64; ++i)
        bytes[i] = static_cast<unsigned char>(dist(gen));
    return base64urlEncode(bytes, 64);
}

std::string GoogleAuth::computeChallenge(const std::string& verifier)
{
    unsigned char digest[32];
    sha256(reinterpret_cast<const unsigned char*>(verifier.c_str()),
           verifier.size(), digest);
    return base64urlEncode(digest, 32);
}

// ---- OAuth flow ----

bool GoogleAuth::authenticate()
{
    std::string verifier  = generateVerifier();
    std::string challenge = computeChallenge(verifier);
    using HaikuHttp::urlEncode;

    std::string authUrl =
        "https://accounts.google.com/o/oauth2/v2/auth"
        "?client_id="             + urlEncode(fClientId)  +
        "&redirect_uri="          + urlEncode("http://127.0.0.1:8765/oauth2callback") +
        "&response_type=code"
        "&scope="                 + urlEncode("https://www.googleapis.com/auth/tasks") +
        "&code_challenge="        + urlEncode(challenge)  +
        "&code_challenge_method=S256"
        "&access_type=offline"
        "&prompt=consent";

    // Open the URL in Haiku's default browser
    std::string cmd = "open-url \"" + authUrl + "\" &";
    system(cmd.c_str());

    std::string code = waitForCode(8765);
    if (code.empty()) return false;

    return exchangeCode(code, verifier);
}

std::string GoogleAuth::waitForCode(int port)
{
    int serverFd = socket(AF_INET, SOCK_STREAM, 0);
    if (serverFd < 0) return "";

    int opt = 1;
    setsockopt(serverFd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port        = htons(static_cast<uint16_t>(port));

    if (bind(serverFd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        close(serverFd); return "";
    }
    if (listen(serverFd, 1) < 0) {
        close(serverFd); return "";
    }

    struct sockaddr_in clientAddr;
    socklen_t clientLen = sizeof(clientAddr);
    int clientFd = accept(serverFd,
                          reinterpret_cast<struct sockaddr*>(&clientAddr), &clientLen);
    close(serverFd);
    if (clientFd < 0) return "";

    char buf[4096];
    int n = recv(clientFd, buf, sizeof(buf) - 1, 0);
    std::string code;

    if (n > 0) {
        buf[n] = '\0';
        std::string req(buf);
        size_t codePos = req.find("code=");
        if (codePos != std::string::npos) {
            codePos += 5;
            size_t codeEnd = req.find_first_of("& \r\n", codePos);
            if (codeEnd == std::string::npos) codeEnd = req.size();
            code = req.substr(codePos, codeEnd - codePos);
        }

        const char* okHtml =
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/html; charset=utf-8\r\n"
            "Connection: close\r\n\r\n"
            "<!DOCTYPE html><html><head><title>GoH-Tasks</title></head>"
            "<body style='font-family:sans-serif;text-align:center;padding:60px'>"
            "<h1 style='color:#1a73e8'>&#10003; Autenticazione riuscita</h1>"
            "<p>Puoi chiudere questa finestra e tornare a GoH-Tasks.</p>"
            "</body></html>\r\n";

        const char* errHtml =
            "HTTP/1.1 400 Bad Request\r\n"
            "Content-Type: text/html; charset=utf-8\r\n"
            "Connection: close\r\n\r\n"
            "<!DOCTYPE html><html><body style='font-family:sans-serif;"
            "text-align:center;padding:60px'>"
            "<h1>&#10007; Autenticazione fallita</h1>"
            "<p>Nessun codice di autorizzazione ricevuto.</p>"
            "</body></html>\r\n";

        const char* page = code.empty() ? errHtml : okHtml;
        send(clientFd, page, strlen(page), 0);
    }
    close(clientFd);
    return code;
}

// ---- Token exchange / refresh (via Haiku Network Kit) ----

bool GoogleAuth::exchangeCode(const std::string& code, const std::string& verifier)
{
    using HaikuHttp::urlEncode;

    std::string body =
        "code="           + urlEncode(code)         +
        "&client_id="     + urlEncode(fClientId)    +
        "&client_secret=" + urlEncode(fClientSecret) +
        "&redirect_uri="  + urlEncode("http://127.0.0.1:8765/oauth2callback") +
        "&grant_type=authorization_code"
        "&code_verifier=" + urlEncode(verifier);

    auto resp = HaikuHttp::post(
        "https://oauth2.googleapis.com/token",
        body,
        {{"Content-Type", "application/x-www-form-urlencoded"}});

    if (resp.body.empty()) return false;

    try {
        JsonValue j = JsonValue::parse(resp.body);
        fAccessToken  = j["access_token"].str();
        fRefreshToken = j["refresh_token"].str();
        double expiresIn = j["expires_in"].num();
        fTokenExpiry  = time(nullptr) + static_cast<time_t>(expiresIn);
        if (fAccessToken.empty()) return false;
        return saveTokens();
    } catch (...) {
        return false;
    }
}

bool GoogleAuth::refreshTokens()
{
    if (fRefreshToken.empty()) return false;
    using HaikuHttp::urlEncode;

    std::string body =
        "client_id="     + urlEncode(fClientId)     +
        "&client_secret=" + urlEncode(fClientSecret) +
        "&refresh_token=" + urlEncode(fRefreshToken) +
        "&grant_type=refresh_token";

    auto resp = HaikuHttp::post(
        "https://oauth2.googleapis.com/token",
        body,
        {{"Content-Type", "application/x-www-form-urlencoded"}});

    if (resp.body.empty()) return false;

    try {
        JsonValue j = JsonValue::parse(resp.body);
        std::string newToken = j["access_token"].str();
        if (newToken.empty()) return false;
        fAccessToken = newToken;
        double expiresIn = j["expires_in"].num();
        fTokenExpiry = time(nullptr) + static_cast<time_t>(expiresIn);
        std::string newRefresh = j["refresh_token"].str();
        if (!newRefresh.empty()) fRefreshToken = newRefresh;
        return saveTokens();
    } catch (...) {
        return false;
    }
}

std::string GoogleAuth::getAccessToken()
{
    BAutolock lock(fLock);
    if (fAccessToken.empty() || fRefreshToken.empty())
        loadTokens();
    if (time(nullptr) < fTokenExpiry - 60)
        return fAccessToken;
    return refreshTokens() ? fAccessToken : std::string();
}
