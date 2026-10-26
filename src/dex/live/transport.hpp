#pragma once
/*  transport.hpp — the two transports the live engine talks through, as interfaces, plus the encoding helpers.

      HttpClient  one blocking request → HttpResponse. status 0 = no complete response. `sent` says whether the request
                  bytes were (or may have been) written: a connect failure before the write is sent=false — for
                  sendTx that is a definite "not sent" (the nonce is free again); after the write, a missing answer is
                  UNKNOWN (the tx may have executed) and is never read as a failure.
      WsConn      a message-oriented WebSocket: open() (blocking connect + handshake), send(text), poll() (next received
                  message, non-blocking), is_open(), close(). The engine never touches a socket itself: the driver
                  moves frames between the engine and a WsConn, so tests and the offline soak inject in-process ones.

    The real implementations (net.hpp) refuse every host but mainnet.zklighter.elliot.ai:443; the testnet host is
    refused by name. Tests that exercise the real clients use 127.0.0.1 with an explicit loopback flag.          */

#include <cctype>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace lighter::live {

struct HttpRequest {
    std::string method = "GET";               // GET | POST
    std::string target;                       // "/api/v1/..." including the query string
    std::string body;
    std::string content_type;                 // POST only
    std::vector<std::pair<std::string, std::string>> headers;
    int timeout_ms = 10'000;
};

struct HttpResponse {
    int status = 0;                           // 0 = no complete HTTP response
    bool sent = true;                         // false = the request was certainly NOT written (connect/TLS failure)
    std::string body;
    std::string error;                        // transport error text when status == 0
};

class HttpClient {
public:
    virtual ~HttpClient() = default;
    virtual HttpResponse request(const HttpRequest& r) = 0;
};

class WsConn {
public:
    virtual ~WsConn() = default;
    virtual bool open() = 0;                                  // blocking connect + handshake; false on failure
    virtual bool send(const std::string& text) = 0;           // false = not queued (closed)
    virtual std::optional<std::string> poll() = 0;            // next received message, non-blocking
    virtual bool is_open() const = 0;
    virtual void close() = 0;
    virtual std::string last_error() const { return {}; }
};

// RFC 3986 percent-encoding of everything but the unreserved set (form values: tx_info JSON, base64 signatures …).
inline std::string url_encode(std::string_view s) {
    static const char* hx = "0123456789ABCDEF";
    std::string o;
    o.reserve(s.size() * 3);
    for (unsigned char c : s) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') o += char(c);
        else { o += '%'; o += hx[c >> 4]; o += hx[c & 15]; }
    }
    return o;
}

inline std::string url_decode(std::string_view s) {
    auto hv = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    std::string o;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size() && hv(s[i + 1]) >= 0 && hv(s[i + 2]) >= 0) {
            o += char(hv(s[i + 1]) * 16 + hv(s[i + 2]));
            i += 2;
        } else if (s[i] == '+') {
            o += ' ';
        } else {
            o += s[i];
        }
    }
    return o;
}

inline std::string form_encode(const std::vector<std::pair<std::string, std::string>>& kv) {
    std::string o;
    for (const auto& [k, v] : kv) {
        if (!o.empty()) o += '&';
        o += url_encode(k) + '=' + url_encode(v);
    }
    return o;
}

// "a=1&b=2" → [(a,1),(b,2)], values decoded. Used by the in-process venue and the tests.
inline std::vector<std::pair<std::string, std::string>> form_decode(std::string_view s) {
    std::vector<std::pair<std::string, std::string>> out;
    size_t a = 0;
    while (a <= s.size()) {
        size_t e = s.find('&', a);
        if (e == std::string_view::npos) e = s.size();
        std::string_view kv = s.substr(a, e - a);
        if (!kv.empty()) {
            size_t eq = kv.find('=');
            out.emplace_back(url_decode(kv.substr(0, eq)), eq == std::string_view::npos ? "" : url_decode(kv.substr(eq + 1)));
        }
        a = e + 1;
    }
    return out;
}

inline std::string form_get(const std::vector<std::pair<std::string, std::string>>& kv, const std::string& k) {
    for (const auto& [a, b] : kv) if (a == k) return b;
    return {};
}

}  // namespace lighter::live
