#include "io/src/udp.hpp"

#include <array>
#include <stdexcept>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <cerrno>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include "proto/messages.hpp"

namespace c2::detail {
namespace {
#ifdef _WIN32
using Socket = SOCKET;
constexpr Socket invalid_socket = INVALID_SOCKET;
void close_socket(Socket s) { closesocket(s); }
struct Winsock {
    Winsock() { WSADATA data{}; if (WSAStartup(MAKEWORD(2, 2), &data) != 0) throw std::runtime_error("WSAStartup failed"); }
    ~Winsock() { WSACleanup(); }
};
Winsock& winsock() { static Winsock instance; return instance; }
#else
using Socket = int;
constexpr Socket invalid_socket = -1;
void close_socket(Socket s) { close(s); }
#endif
}

UdpSocket::UdpSocket(std::uint16_t port) : socket_(invalid_socket) {
#ifdef _WIN32
    (void)winsock();
#endif
    const Socket s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == invalid_socket) throw std::runtime_error("UDP socket creation failed");
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(port);
    if (bind(s, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0) {
        close_socket(s);
        throw std::runtime_error("Cannot bind UDP loopback port " + std::to_string(port));
    }
    socket_ = s;
}

UdpSocket::~UdpSocket() { close_socket(static_cast<Socket>(socket_)); }

void UdpSocket::send(std::uint16_t port, const std::string& data) {
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(port);
    const auto sent = sendto(static_cast<Socket>(socket_), data.data(), static_cast<int>(data.size()), 0,
                             reinterpret_cast<const sockaddr*>(&address), sizeof(address));
    if (sent < 0 || static_cast<std::size_t>(sent) != data.size()) throw std::runtime_error("UDP send failed");
}

std::optional<Datagram> UdpSocket::receive(std::chrono::milliseconds timeout) {
    const Socket s = static_cast<Socket>(socket_);
    fd_set read_set;
    FD_ZERO(&read_set);
    FD_SET(s, &read_set);
    const auto count = timeout.count() < 0 ? 0 : timeout.count();
    timeval tv{static_cast<long>(count / 1000), static_cast<long>((count % 1000) * 1000)};
#ifdef _WIN32
    const int ready = select(0, &read_set, nullptr, nullptr, &tv);
#else
    const int ready = select(s + 1, &read_set, nullptr, nullptr, &tv);
#endif
    if (ready == 0) return std::nullopt;
    if (ready < 0) throw std::runtime_error("UDP select failed");
    std::array<char, max_datagram_bytes + 1> buffer{};
    sockaddr_in sender{};
#ifdef _WIN32
    int sender_size = sizeof(sender);
#else
    socklen_t sender_size = sizeof(sender);
#endif
    const auto length = recvfrom(s, buffer.data(), static_cast<int>(buffer.size()), 0,
                                 reinterpret_cast<sockaddr*>(&sender), &sender_size);
    if (length < 0) throw std::runtime_error("UDP receive failed or datagram exceeds limit");
    if (static_cast<std::size_t>(length) > max_datagram_bytes) throw std::runtime_error("UDP datagram exceeds 1200 bytes");
    return Datagram{std::string(buffer.data(), static_cast<std::size_t>(length)),
                    ntohl(sender.sin_addr.s_addr), ntohs(sender.sin_port)};
}

}  // namespace c2::detail
