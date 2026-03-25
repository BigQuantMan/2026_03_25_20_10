#pragma once
#include "live_order_types.hpp"

class IOrderGateway {
public:
    virtual ~IOrderGateway() = default;
    virtual ExecutionReport submit_order(const LiveOrderRequest& req) = 0;
};
