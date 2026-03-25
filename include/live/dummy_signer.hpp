#pragma once
#include "signer.hpp"
#include <string>

class DummySigner : public ISigner {
public:
    std::string sign(const std::string& payload) override;
};
