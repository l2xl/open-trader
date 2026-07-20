// Open Trader
// Copyright (c) 2026 l2xl (l2xl/at/proton.me)
// Distributed under the Intellectual Property Reserve License, v2 (IPRL)

#ifndef DATA_ENTITIES_ENUMS_HPP
#define DATA_ENTITIES_ENUMS_HPP

#include <string>
#include <string_view>
#include <array>
#include <glaze/glaze.hpp>

namespace scratcher::data {

class OrderSide {
private:
    const char* name_;
    int id_;
    
    // Private constructor to prevent external instantiation
    constexpr OrderSide(const char* name, int id) : name_(name), id_(id) {}
    
    // Static array of instances
    static const std::array<OrderSide, 2> instances_;
    
public:
    // Named references to array elements
    static const OrderSide& BUY;
    static const OrderSide& SELL;
    
    // Accessors
    constexpr const char* name() const { return name_; }
    constexpr int id() const { return id_; }
    
    // Conversion operators
    operator const char*() const { return name_; }
    operator int() const { return id_; }
    
    // Comparison operators
    constexpr bool operator==(const OrderSide& other) const {
        return id_ == other.id_;
    }
    
    constexpr bool operator!=(const OrderSide& other) const {
        return id_ != other.id_;
    }
    
    // Get all values (for iteration)
    static const std::array<OrderSide, 2>& values() {
        return instances_;
    }
    
    // String selection by iteration
    static const OrderSide& select(std::string_view str) {
        for (const auto& instance : instances_) {
            if (std::string_view(instance.name()) == str) {
                return instance;
            }
        }
        throw std::invalid_argument("Invalid OrderSide string");
    }
};

enum class OrderType {
    Market,
    Limit
};

enum class TimeInForce {
    GTC,    // Good Till Cancel
    IOC,    // Immediate Or Cancel
    FOK,    // Fill Or Kill
    PostOnly
};

// Helper functions for string conversion
std::string to_string(const OrderSide& side);
std::string to_string(OrderType type);

OrderType order_type_from_string(const std::string& str);

} // namespace scratcher::data

template <>
struct glz::meta<scratcher::data::TimeInForce> {
    using enum scratcher::data::TimeInForce;
    static constexpr auto value = enumerate(
        "GTC", GTC,
        "IOC", IOC,
        "FOK", FOK,
        "PostOnly", PostOnly
    );
};

#endif // DATA_ENTITIES_ENUMS_HPP
