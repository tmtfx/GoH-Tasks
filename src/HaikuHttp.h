#pragma once
// Synchronous HTTP helper using Haiku's native Network Kit.
// BHttpRequest/BUrlProtocolListener live in BPrivate::Network (private headers).

#include <string>
#include <utility>
#include <vector>

#include <DataIO.h>
#include <OS.h>
#include <Url.h>

// Private Network Services headers (private/netservices/)
#include <HttpHeaders.h>
#include <HttpRequest.h>
#include <UrlProtocolListener.h>
#include <UrlRequest.h>

// Expose needed types at global scope, mirroring the rest of BeAPI convention.
using BPrivate::Network::BHttpHeaders;
using BPrivate::Network::BHttpRequest;
using BPrivate::Network::BUrlProtocolListener;
using BPrivate::Network::BUrlRequest;

namespace HaikuHttp {

struct Response {
    std::string body;
    int32 statusCode = 0;
    bool ok() const { return statusCode >= 200 && statusCode < 300; }
};

// Collects data via callbacks; request thread fills it, wait_for_thread syncs.
class SyncListener : public BUrlProtocolListener {
public:
    void DataReceived(BUrlRequest* /*caller*/, const char* data,
                      off_t /*position*/, ssize_t size) override
    {
        fResponse.body.append(data, static_cast<size_t>(size));
    }

    void RequestCompleted(BUrlRequest* caller, bool /*success*/) override
    {
        if (BHttpRequest* http = dynamic_cast<BHttpRequest*>(caller))
            fResponse.statusCode = http->Result().StatusCode();
    }

    Response fResponse;
};

inline Response request(const std::string& method,
                        const std::string& url,
                        const std::string& body,
                        const std::vector<std::pair<std::string,std::string>>& headers)
{
    SyncListener listener;

    // BUrl(const char*, bool encode) — use false to avoid double-encoding.
    // Construct BHttpRequest directly (BUrlProtocolRoster removed in current Haiku).
    BHttpRequest* req = new BHttpRequest(BUrl(url.c_str(), false),
                                         /*ssl=*/false, "HTTP", &listener);
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

    Response resp = listener.fResponse;
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
