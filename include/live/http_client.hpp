#pragma once
#include <string>
#include <vector>

struct HttpHeader {
    std::string key;
    std::string value;
};

struct HttpRequest {
    std::string method;   // GET / POST
    std::string url;
    std::string body;
    std::vector<HttpHeader> headers;
};

struct HttpResponse {
    int         status_code = 0;
    std::string body;
    std::string error_message;
};

class IHttpClient {
public:
    virtual ~IHttpClient() = default;
    virtual HttpResponse send(const HttpRequest& request) = 0;
};
