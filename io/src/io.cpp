#include "io/api/io.hpp"

#include <array>
#include <stdexcept>

#ifdef _WIN32
#include <windows.h>
#include <bcrypt.h>
#else
#include <fstream>
#endif

#include "io/src/udp.hpp"

namespace c2 {
namespace {
std::uint16_t device_port(const Ports& ports, const std::string& id) {
    if (id == center_id) return ports.center;
    if (id == car_id) return ports.car;
    if (id == uav_id) return ports.uav;
    if (id == dog_id) return ports.dog;
    throw std::runtime_error("Unknown device ID: " + id);
}
void check_ports(const Ports& ports) {
    const std::array<std::uint16_t, 4> values{ports.center, ports.car, ports.uav, ports.dog};
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (values[i] == 0) throw std::runtime_error("Port must be between 1 and 65535");
        for (std::size_t j = 0; j < i; ++j)
            if (values[i] == values[j]) throw std::runtime_error("Device ports must be distinct");
    }
}
}

std::string make_id() {
    std::array<unsigned char, 16> bytes{};
#ifdef _WIN32
    if (BCryptGenRandom(nullptr, bytes.data(), static_cast<ULONG>(bytes.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0)
        throw std::runtime_error("Could not generate random ID");
#else
    std::ifstream random("/dev/urandom", std::ios::binary);
    if (!random.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())))
        throw std::runtime_error("Could not generate random ID");
#endif
    constexpr char hex[] = "0123456789abcdef";
    std::string result;
    result.reserve(bytes.size() * 2);
    for (const auto b : bytes) { result.push_back(hex[b >> 4]); result.push_back(hex[b & 15]); }
    return result;
}

std::uint64_t unix_time_ms() {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
}
std::uint64_t monotonic_time_ms() {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}

struct Io::Impl {
    explicit Impl(Ports input) : ports(input), socket((check_ports(input), input.center)) {}
    Ports ports;
    detail::UdpSocket socket;
};
Io::Io(Ports ports) : impl_(std::make_unique<Impl>(ports)) {}
Io::~Io() = default;

void Io::send(const Message& message) {
    if (message.envelope.src_id != center_id) throw std::runtime_error("Only c2 can send from the center socket");
    if (const auto* task = std::get_if<Task>(&message.body)) {
        const auto now = unix_time_ms();
        if (now >= task->expire_at_unix_ms || task->expire_at_unix_ms - now <= task->max_clock_skew_ms)
            throw std::runtime_error("Task expired before send");
    }
    impl_->socket.send(device_port(impl_->ports, message.envelope.dst_id), encode_json(message));
}

std::optional<Message> Io::receive(std::chrono::milliseconds timeout) {
    const auto datagram = impl_->socket.receive(timeout);
    if (!datagram) return std::nullopt;
    auto message = decode_json(datagram->data);
    if (message.envelope.dst_id != center_id || message.envelope.src_id == center_id ||
        datagram->address != 0x7f000001U || datagram->port != device_port(impl_->ports, message.envelope.src_id))
        throw std::runtime_error("Unexpected UDP sender or destination");
    return message;
}

}  // namespace c2
