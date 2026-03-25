#include "../../include/live/hmac_sha256_signer.hpp"
#include <openssl/hmac.h>
#include <iomanip>
#include <sstream>

std::string HmacSha256Signer::sign(const std::string& payload) {
    unsigned char hash[EVP_MAX_MD_SIZE];
    unsigned int len = 0;

    HMAC(EVP_sha256(),
         secret_.data(), static_cast<int>(secret_.size()),
         reinterpret_cast<const unsigned char*>(payload.data()), payload.size(),
         hash, &len);

    std::ostringstream oss;
    for (unsigned int i = 0; i < len; ++i) {
        oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(hash[i]);
    }
    return oss.str();
}
