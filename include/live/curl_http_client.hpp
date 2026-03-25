#pragma once
#include "http_client.hpp"

class CurlHttpClient : public IHttpClient {
public:
    HttpResponse send(const HttpRequest& request) override;
};
