#pragma once
#include <string>
#include <OS.h>
#include <Locker.h>

class BWindow;

class GoogleAuth {
public:
    GoogleAuth();
    ~GoogleAuth();

    bool loadConfig();
    bool saveConfig(const std::string& clientId, const std::string& clientSecret);

    bool isAuthenticated();
    bool authenticate(); // opens browser, waits for code, exchanges for tokens
    std::string getAccessToken(); // refreshes if needed, returns valid token

    const std::string& clientId() const { return fClientId; }
    const std::string& clientSecret() const { return fClientSecret; }
    bool hasCredentials() const { return !fClientId.empty() && !fClientSecret.empty(); }

private:
    bool loadTokens();
    bool saveTokens();
    bool refreshTokens();
    bool exchangeCode(const std::string& code, const std::string& verifier);
    std::string waitForCode(int port); // blocks, listens on local HTTP server
    static std::string generateVerifier();
    static std::string computeChallenge(const std::string& verifier);
    static std::string base64urlEncode(const unsigned char* data, size_t len);

    std::string fClientId;
    std::string fClientSecret;
    std::string fAccessToken;
    std::string fRefreshToken;
    time_t fTokenExpiry = 0;
    std::string fSettingsDir;
    BLocker fLock;
};
