#include "beam/discovery.hpp"
#include <iostream>
#include <unistd.h>

int main() {
    try {
        const auto name = "Beam-Test-" + std::to_string(getpid());
        {
            beam::ReceiverAdvertisement advertisement(4269, name);
            const auto registered = advertisement.name();
            const auto devices = beam::discover_devices();
            bool found = false;
            for (const auto& device : devices)
                if (device.name == registered)
                    for (const auto& endpoint : device.endpoints)
                        found |= endpoint.port == 4269;
            if (!found)
                throw std::runtime_error("live mDNS registration did not resolve to an endpoint");
            advertisement.check();
        }
        const auto devices = beam::discover_devices();
        for (const auto& device : devices)
            if (device.name == name)
                throw std::runtime_error("withdrawn receiver still discoverable");
        std::cout << "Live mDNS advertisement, browse, address resolution, and withdrawal passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
