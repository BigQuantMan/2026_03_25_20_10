#pragma once
#include <string>

class ISigner {
public:
    virtual ~ISigner() = default;
    virtual std::string sign(const std::string& payload) = 0;
};
