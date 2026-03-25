#pragma once
#include "http_client.hpp"

class DummyHttpClient : public IHttpClient {
public:
    HttpResponse send(const HttpRequest& request) override;
};
