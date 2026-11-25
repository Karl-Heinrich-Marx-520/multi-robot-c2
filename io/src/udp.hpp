#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>

namespace c2::detail {

struct Datagram {
    std::string data;
    std::uint32_t address;
    std::uint16_t port;
};

class UdpSocket {
public:
    explicit UdpSocket(std::uint16_t port);
    ~UdpSocket();
    UdpSocket(const UdpSocket&) = delete;
    UdpSocket& operator=(const UdpSocket&) = delete;
    void send(std::uint16_t port, const std::string& data);
    std::optional<Datagram> receive(std::chrono::milliseconds timeout);
private:
#ifdef _WIN32
    std::uintptr_t socket_;
#else
    int socket_;
#endif
};

}  // namespace c2::detail
