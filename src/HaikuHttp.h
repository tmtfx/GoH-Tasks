#pragma once
// Synchronous HTTP helper — Haiku native Network Kit (private/netservices).
// Response body goes to BMallocIO output; status via BHttpResult downcast.

#include <string>
#include <utility>
#include <vector>

#include <DataIO.h>
#include <OS.h>
#include <Url.h>

#include <HttpHeaders.h>
#include <HttpRequest.h>
#include <HttpResult.h>
#include <UrlProtocolListener.h>
#include <UrlRequest.h>

using BPrivate::Network::BHttpHeaders;
using BPrivate::Network::BHttpRequest;
using BPrivate::Network::BHttpResult;
using BPrivate::Network::BUrlProtocolListener;
using BPrivate::Network::BUrlRequest;

namespace HaikuHttp {

struct Response {
    std::string body;
    int32 statusCode = 0;
    bool ok() const { return statusCode >= 200 && statusCode < 300; }
};

// Only captures the HTTP status code; body is written to BMallocIO by the request.
class SyncListener : public BUrlProtocolListener {
public:
    void RequestCompleted(BUrlRequest* caller, bool success) override
    {
        if (BHttpRequest* http = dynamic_cast<BHttpRequest*>(caller)) {
            const BHttpResult* r =
                dynamic_cast<const BHttpResult*>(&http->Result());
            fStatusCode = r ? r->StatusCode() : (success ? 200 : 0);
        }
    }
    int32 fStatusCode = 0;
};

inline Response request(const std::string& method,
                        const std::string& url,
                        const std::string& body,
                        const std::vector<std::pair<std::string,std::string>>& headers)
{
    SyncListener listener;
    BMallocIO responseBody;

    bool ssl = url.size() >= 5 && url.substr(0, 5) == "https";

    // Constructor: BHttpRequest(url, output, ssl, protocolName, listener, context)
    BHttpRequest* req = new BHttpRequest(
        BUrl(url.c_str(), false),
        &responseBody,
        ssl,
        "HTTP",
        &listener,
        nullptr);

    if (!req)
        return {};

    req->SetMethod(method.c_str());

    if (!headers.empty()) {
        BHttpHeaders* hdr = new BHttpHeaders();
        for (auto& [k, v] : headers)
            hdr->AddHeader(k.c_str(), v.c_str());
        req->AdoptHeaders(hdr);
    }

    if (!body.empty()) {
        BMemoryIO* io = new BMemoryIO(body.c_str(), body.size());
        req->AdoptInputData(io, static_cast<ssize_t>(body.size()));
    }

    status_t exitStatus;
    wait_for_thread(req->Run(), &exitStatus);

    Response resp;
    resp.statusCode = listener.fStatusCode;
    resp.body = std::string(
        static_cast<const char*>(responseBody.Buffer()),
        responseBody.BufferLength());

    delete req;
    return resp;
}

inline Response get(const std::string& url,
                    const std::vector<std::pair<std::string,std::string>>& headers = {})
{
    return request("GET", url, "", headers);
}

inline Response post(const std::string& url, const std::string& body,
                     const std::vector<std::pair<std::string,std::string>>& headers = {})
{
    return request("POST", url, body, headers);
}

inline Response patch(const std::string& url, const std::string& body,
                      const std::vector<std::pair<std::string,std::string>>& headers = {})
{
    return request("PATCH", url, body, headers);
}

inline Response put(const std::string& url, const std::string& body,
                    const std::vector<std::pair<std::string,std::string>>& headers = {})
{
    return request("PUT", url, body, headers);
}

inline Response del(const std::string& url,
                    const std::vector<std::pair<std::string,std::string>>& headers = {})
{
    return request("DELETE", url, "", headers);
}

// RFC 3986 percent-encoding
inline std::string urlEncode(const std::string& s)
{
    static const char kHex[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(s.size() * 3);
    for (unsigned char c : s) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
            out += static_cast<char>(c);
        else {
            out += '%';
            out += kHex[c >> 4];
            out += kHex[c & 0x0F];
        }
    }
    return out;
}

} // namespace HaikuHttp
