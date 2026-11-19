#include <iostream>

#include "io/mock/mock_link.hpp"

int main() {
    c2::MockLink link;
    const auto subscription = link.subscribe([](const c2::IoEvent&) {});
    std::cout << "c2_center scaffold (protocol v" << c2::protocol_version << ").\n"
              << "IO interface and manual mock are available.\n"
              << "UDP, task routing and session recovery are not implemented.\n";
    return 0;
}
