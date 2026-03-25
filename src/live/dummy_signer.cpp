#include "../../include/live/dummy_signer.hpp"
#include <sstream>

std::string DummySigner::sign(const std::string& payload) {
    std::ostringstream oss;
    oss << "DUMMY_SIG_LEN_" << payload.size();
    return oss.str();
}
