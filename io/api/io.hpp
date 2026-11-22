#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include "proto/messages.hpp"

namespace c2 {

inline constexpr const char* center_id = "c2";
inline constexpr const char* car_id = "car_01";
inline constexpr const char* uav_id = "uav_01";
inline constexpr const char* dog_id = "dog_01";

struct Ports {
    std::uint16_t center{19000};
    std::uint16_t car{19001};
    std::uint16_t uav{19002};
    std::uint16_t dog{19003};
};

// One UTF-8 JSON object per datagram. Throws std::runtime_error on invalid input.
std::string encode_json(const Message& message);
Message decode_json(const std::string& json);
std::string make_id();
std::uint64_t unix_time_ms();
std::uint64_t monotonic_time_ms();

// Single-threaded; the socket closes when this object is destroyed.
class Io {
public:
    explicit Io(Ports ports = {});
    ~Io();
    Io(const Io&) = delete;
    Io& operator=(const Io&) = delete;

    // Returns after the OS accepts the datagram, not after device acceptance.
    void send(const Message& message);
    // Returns nullopt on timeout; malformed/unexpected datagrams throw.
    std::optional<Message> receive(std::chrono::milliseconds timeout);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace c2
