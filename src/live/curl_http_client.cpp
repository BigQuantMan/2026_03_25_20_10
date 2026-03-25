#include "../../include/live/curl_http_client.hpp"
#include <curl/curl.h>

static size_t write_callback(void* contents, size_t size, size_t nmemb, void* userp) {
    const size_t total = size * nmemb;
    std::string* out = static_cast<std::string*>(userp);
    out->append(static_cast<char*>(contents), total);
    return total;
}

HttpResponse CurlHttpClient::send(const HttpRequest& request) {
    HttpResponse resp;
    CURL* curl = curl_easy_init();
    if (!curl) {
        resp.error_message = "curl_easy_init failed";
        return resp;
    }

    curl_easy_setopt(curl, CURLOPT_URL, request.url.c_str());
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, request.method.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp.body);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

    struct curl_slist* headers = nullptr;
    for (const auto& h : request.headers) {
        std::string line = h.key + ": " + h.value;
        headers = curl_slist_append(headers, line.c_str());
    }
    if (headers) curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    if (!request.body.empty()) {
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request.body.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, request.body.size());
    }

    const CURLcode code = curl_easy_perform(curl);
    if (code != CURLE_OK) {
        resp.error_message = curl_easy_strerror(code);
    }

    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    resp.status_code = static_cast<int>(status);

    if (headers) curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return resp;
}
