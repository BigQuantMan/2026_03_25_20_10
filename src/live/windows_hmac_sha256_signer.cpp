#include "../../include/live/windows_hmac_sha256_signer.hpp"

#ifdef _WIN32
#include <windows.h>
#include <bcrypt.h>
#include <vector>
#pragma comment(lib, "bcrypt.lib")

namespace {

std::string to_hex(const unsigned char* data, size_t n) {
    static const char* digits = "0123456789abcdef";
    std::string out;
    out.resize(n * 2);
    for (size_t i = 0; i < n; ++i) {
        out[2 * i]     = digits[(data[i] >> 4) & 0xF];
        out[2 * i + 1] = digits[data[i] & 0xF];
    }
    return out;
}

} // namespace

std::string WindowsHmacSha256Signer::sign(const std::string& payload) {
    BCRYPT_ALG_HANDLE hAlg = nullptr;
    BCRYPT_HASH_HANDLE hHash = nullptr;
    DWORD obj_len = 0;
    DWORD cb_data = 0;
    DWORD hash_len = 0;

    if (BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, nullptr, BCRYPT_ALG_HANDLE_HMAC_FLAG) < 0) {
        return {};
    }

    if (BCryptGetProperty(hAlg, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&obj_len), sizeof(obj_len), &cb_data, 0) < 0) {
        BCryptCloseAlgorithmProvider(hAlg, 0);
        return {};
    }
    if (BCryptGetProperty(hAlg, BCRYPT_HASH_LENGTH, reinterpret_cast<PUCHAR>(&hash_len), sizeof(hash_len), &cb_data, 0) < 0) {
        BCryptCloseAlgorithmProvider(hAlg, 0);
        return {};
    }

    std::vector<unsigned char> obj(obj_len);
    std::vector<unsigned char> hash(hash_len);

    if (BCryptCreateHash(hAlg,
                         &hHash,
                         obj.data(),
                         static_cast<ULONG>(obj.size()),
                         reinterpret_cast<PUCHAR>(const_cast<char*>(secret_.data())),
                         static_cast<ULONG>(secret_.size()),
                         0) < 0) {
        BCryptCloseAlgorithmProvider(hAlg, 0);
        return {};
    }

    if (BCryptHashData(hHash,
                       reinterpret_cast<PUCHAR>(const_cast<char*>(payload.data())),
                       static_cast<ULONG>(payload.size()),
                       0) < 0) {
        BCryptDestroyHash(hHash);
        BCryptCloseAlgorithmProvider(hAlg, 0);
        return {};
    }

    if (BCryptFinishHash(hHash, hash.data(), static_cast<ULONG>(hash.size()), 0) < 0) {
        BCryptDestroyHash(hHash);
        BCryptCloseAlgorithmProvider(hAlg, 0);
        return {};
    }

    BCryptDestroyHash(hHash);
    BCryptCloseAlgorithmProvider(hAlg, 0);
    return to_hex(hash.data(), hash.size());
}

#else
std::string WindowsHmacSha256Signer::sign(const std::string&) {
    return {};
}
#endif
