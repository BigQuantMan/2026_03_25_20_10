#pragma once
#include "http_client.hpp"

class WinHttpClient : public IHttpClient {
public:
    HttpResponse send(const HttpRequest& request) override;
};
