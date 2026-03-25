#include "../../include/live/dummy_http_client.hpp"

HttpResponse DummyHttpClient::send(const HttpRequest& request) {
    HttpResponse resp;
    resp.status_code = 200;
    resp.body = std::string("DUMMY_HTTP_CLIENT method=") + request.method + " url=" + request.url;
    return resp;
}
