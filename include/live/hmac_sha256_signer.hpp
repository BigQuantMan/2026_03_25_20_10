#pragma once
#include "signer.hpp"
#include <string>

class HmacSha256Signer : public ISigner {
public:
    explicit HmacSha256Signer(std::string secret)
        : secret_(std::move(secret)) {}

    std::string sign(const std::string& payload) override;

private:
    std::string secret_;
};
