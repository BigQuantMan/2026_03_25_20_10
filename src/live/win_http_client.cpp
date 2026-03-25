#include "../../include/live/win_http_client.hpp"

#ifdef _WIN32
#include <windows.h>
#include <winhttp.h>
#include <string>
#include <vector>
#pragma comment(lib, "winhttp.lib")

namespace {

std::wstring to_wide(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    if (n <= 0) return {};
    std::wstring buffer(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, buffer.data(), n);
    if (!buffer.empty() && buffer.back() == L'\0') buffer.pop_back();
    return buffer;
}

std::string to_utf8(const std::wstring& s) {
    if (s.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (n <= 0) return {};
    std::string buffer(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.c_str(), -1, buffer.data(), n, nullptr, nullptr);
    if (!buffer.empty() && buffer.back() == '\0') buffer.pop_back();
    return buffer;
}

bool crack_url(const std::string& url, URL_COMPONENTS& uc, std::wstring& host, std::wstring& path) {
    std::wstring wurl = to_wide(url);
    ZeroMemory(&uc, sizeof(uc));
    uc.dwStructSize = sizeof(uc);
    uc.dwSchemeLength = static_cast<DWORD>(-1);
    uc.dwHostNameLength = static_cast<DWORD>(-1);
    uc.dwUrlPathLength = static_cast<DWORD>(-1);
    uc.dwExtraInfoLength = static_cast<DWORD>(-1);

    if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &uc)) {
        return false;
    }

    host.assign(uc.lpszHostName, uc.dwHostNameLength);
    path.assign(uc.lpszUrlPath, uc.dwUrlPathLength);
    if (uc.dwExtraInfoLength > 0 && uc.lpszExtraInfo) {
        path.append(uc.lpszExtraInfo, uc.dwExtraInfoLength);
    }
    return true;
}

} // namespace

HttpResponse WinHttpClient::send(const HttpRequest& request) {
    HttpResponse resp;

    URL_COMPONENTS uc{};
    std::wstring host;
    std::wstring path;
    if (!crack_url(request.url, uc, host, path)) {
        resp.error_message = "WinHttpCrackUrl failed";
        return resp;
    }

    const bool secure = (uc.nScheme == INTERNET_SCHEME_HTTPS);
    const wchar_t* verb = (request.method == "POST") ? L"POST" : L"GET";

    HINTERNET hSession = WinHttpOpen(L"trackA-live-vs/1.0",
                                     WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                     WINHTTP_NO_PROXY_NAME,
                                     WINHTTP_NO_PROXY_BYPASS,
                                     0);
    if (!hSession) {
        resp.error_message = "WinHttpOpen failed";
        return resp;
    }

    HINTERNET hConnect = WinHttpConnect(hSession, host.c_str(), uc.nPort, 0);
    if (!hConnect) {
        resp.error_message = "WinHttpConnect failed";
        WinHttpCloseHandle(hSession);
        return resp;
    }

    HINTERNET hRequest = WinHttpOpenRequest(hConnect,
                                            verb,
                                            path.c_str(),
                                            nullptr,
                                            WINHTTP_NO_REFERER,
                                            WINHTTP_DEFAULT_ACCEPT_TYPES,
                                            secure ? WINHTTP_FLAG_SECURE : 0);
    if (!hRequest) {
        resp.error_message = "WinHttpOpenRequest failed";
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return resp;
    }

    const int timeout_ms = 30000;
    WinHttpSetTimeouts(hRequest, timeout_ms, timeout_ms, timeout_ms, timeout_ms);

    std::wstring header_blob;
    for (const auto& h : request.headers) {
        header_blob += to_wide(h.key + ": " + h.value + "\r\n");
    }

    BOOL ok = WinHttpSendRequest(
        hRequest,
        header_blob.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : header_blob.c_str(),
        header_blob.empty() ? 0 : static_cast<DWORD>(header_blob.size()),
        request.body.empty() ? WINHTTP_NO_REQUEST_DATA : const_cast<char*>(request.body.data()),
        static_cast<DWORD>(request.body.size()),
        static_cast<DWORD>(request.body.size()),
        0);

    if (!ok) {
        resp.error_message = "WinHttpSendRequest failed";
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return resp;
    }

    ok = WinHttpReceiveResponse(hRequest, nullptr);
    if (!ok) {
        resp.error_message = "WinHttpReceiveResponse failed";
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return resp;
    }

    DWORD status = 0;
    DWORD status_size = sizeof(status);
    WinHttpQueryHeaders(hRequest,
                        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX,
                        &status,
                        &status_size,
                        WINHTTP_NO_HEADER_INDEX);
    resp.status_code = static_cast<int>(status);

    std::string body;
    for (;;) {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(hRequest, &available)) {
            resp.error_message = "WinHttpQueryDataAvailable failed";
            break;
        }
        if (available == 0) {
            break;
        }

        std::vector<char> buffer(static_cast<size_t>(available));
        DWORD read = 0;
        if (!WinHttpReadData(hRequest, buffer.data(), available, &read)) {
            resp.error_message = "WinHttpReadData failed";
            break;
        }
        body.append(buffer.data(), buffer.data() + read);
    }
    resp.body = std::move(body);

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return resp;
}

#else
HttpResponse WinHttpClient::send(const HttpRequest&) {
    HttpResponse resp;
    resp.error_message = "WinHttpClient is only available on Windows.";
    return resp;
}
#endif
