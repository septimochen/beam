#include "beam/discovery.hpp"
#include <arpa/inet.h>
#include <array>
#include <atomic>
#include <dns_sd.h>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <unistd.h>

// A pipe-backed DNS-SD daemon substitute exercises the real callback/event loop
// without LAN access. Each reference owns a ready notification and one batch.
struct _DNSServiceRef_t {
    std::array<int, 2> descriptors{};
    std::function<void(DNSServiceRef)> deliver;
};
namespace {
std::atomic<int> live = 0;
int resolves = 0;
int scenario = 0;
std::uint16_t advertised_port = 0;
void require(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}
DNSServiceErrorType create(DNSServiceRef* output, std::function<void(DNSServiceRef)> deliver) {
    auto ref = std::make_unique<_DNSServiceRef_t>();
    if (pipe(ref->descriptors.data()) != 0)
        return kDNSServiceErr_Unknown;
    ref->deliver = std::move(deliver);
    const char notification = 'x';
    require(write(ref->descriptors[1], &notification, 1) == 1, "pipe notification failed");
    *output = ref.release();
    ++live;
    return kDNSServiceErr_NoError;
}
template <typename Error, typename Action> void expect_error(Action action) {
    bool caught = false;
    try {
        action();
    } catch (const Error&) {
        caught = true;
    }
    require(caught, "expected error");
    require(live == 0, "reference leaked on failure");
}
} // namespace
extern "C" {
int DNSSD_API DNSServiceRefSockFD(DNSServiceRef ref) { return ref->descriptors[0]; }
void DNSSD_API DNSServiceRefDeallocate(DNSServiceRef ref) {
    close(ref->descriptors[0]);
    close(ref->descriptors[1]);
    delete ref;
    --live;
}
DNSServiceErrorType DNSSD_API DNSServiceProcessResult(DNSServiceRef ref) {
    char notification = 0;
    if (read(ref->descriptors[0], &notification, 1) != 1)
        return kDNSServiceErr_Unknown;
    ref->deliver(ref);
    return kDNSServiceErr_NoError;
}
DNSServiceErrorType DNSSD_API DNSServiceBrowse(DNSServiceRef* ref, DNSServiceFlags, std::uint32_t,
                                               const char* type, const char* domain,
                                               DNSServiceBrowseReply callback, void* context) {
    require(std::string_view(type) == "_beam._udp" && std::string_view(domain) == "local.",
            "discovery escaped LAN domain");
    if (scenario == 5)
        return kDNSServiceErr_Unknown;
    return create(ref, [=](DNSServiceRef handle) {
        if (scenario == 6) {
            callback(handle, 0, 0, kDNSServiceErr_Unknown, nullptr, nullptr, nullptr, context);
            return;
        }
        if (scenario == 7) {
            for (int i = 0; i < 140; ++i) {
                const auto name = "Receiver" + std::to_string(i);
                callback(handle, kDNSServiceFlagsAdd, 1, 0, name.c_str(), type, domain, context);
            }
            return;
        }
        callback(handle, kDNSServiceFlagsAdd, 1, 0, "Laptop", type, domain, context);
        callback(handle, kDNSServiceFlagsAdd, 1, 0, "Laptop", type, domain, context);
        callback(handle, kDNSServiceFlagsAdd, 2, 0, "Laptop", "_beam._udp.", domain, context);
        callback(handle, kDNSServiceFlagsAdd, 1, 0, "bad\033name", type, domain, context);
        callback(handle, kDNSServiceFlagsAdd, 1, 0, "WrongDomain", type, "example.com.", context);
        callback(handle, kDNSServiceFlagsAdd, 1, 0, "Gone", type, domain, context);
        callback(handle, 0, 1, 0, "Gone", type, domain, context);
    });
}
DNSServiceErrorType DNSSD_API DNSServiceResolve(DNSServiceRef* ref, DNSServiceFlags,
                                                std::uint32_t index, const char*, const char*,
                                                const char*, DNSServiceResolveReply callback,
                                                void* context) {
    ++resolves;
    return create(ref, [=](DNSServiceRef handle) {
        std::vector<unsigned char> txt{3, 'v', '=', '2'};
        if (scenario == 1)
            txt[3] = '9';
        if (scenario == 2)
            txt[0] = 10;
        if (scenario == 3)
            txt.insert(txt.end(), {3, 'v', '=', '2'});
        callback(handle, 0, index, 0, "Laptop._beam._udp.local.",
                 scenario == 4 ? "outside.example.com." : "laptop.local.", htons(4269),
                 static_cast<std::uint16_t>(txt.size()), txt.data(), context);
    });
}
DNSServiceErrorType DNSSD_API DNSServiceQueryRecord(DNSServiceRef* ref, DNSServiceFlags,
                                                    std::uint32_t index, const char*,
                                                    std::uint16_t type, std::uint16_t,
                                                    DNSServiceQueryRecordReply callback,
                                                    void* context) {
    return create(ref, [=](DNSServiceRef handle) {
        auto address = [&](const char* ip, DNSServiceFlags flags, std::uint32_t interface) {
            std::array<unsigned char, 16> bytes{};
            require(inet_pton(type == kDNSServiceType_A ? AF_INET : AF_INET6, ip, bytes.data()) ==
                        1,
                    "invalid test IP");
            callback(handle, flags, interface, 0, "laptop.local.", type, kDNSServiceClass_IN,
                     type == kDNSServiceType_A ? 4 : 16, bytes.data(), 120, context);
        };
        if (type == kDNSServiceType_A) {
            address("192.168.1.50", kDNSServiceFlagsAdd, index);
            address("192.168.1.50", kDNSServiceFlagsAdd, index);
            address("192.168.1.51", kDNSServiceFlagsAdd, index);
            address("192.168.1.51", 0, index);
            address("192.168.1.52", kDNSServiceFlagsAdd, index + 10);
            address("224.0.0.1", kDNSServiceFlagsAdd, index);
            address("0.0.0.0", kDNSServiceFlagsAdd, index);
            if (scenario == 10)
                for (int i = 1; i <= 32; ++i) {
                    const auto ip = "10.0.0." + std::to_string(i);
                    address(ip.c_str(), kDNSServiceFlagsAdd, index);
                }
        } else {
            address("fe80::1", kDNSServiceFlagsAdd, index);
            address("::", kDNSServiceFlagsAdd, index);
            address("ff02::1", kDNSServiceFlagsAdd, index);
        }
    });
}
DNSServiceErrorType DNSSD_API DNSServiceRegister(DNSServiceRef* ref, DNSServiceFlags, std::uint32_t,
                                                 const char*, const char* type, const char* domain,
                                                 const char*, std::uint16_t port,
                                                 std::uint16_t length, const void* txt,
                                                 DNSServiceRegisterReply callback, void* context) {
    require(length == 4 && static_cast<const char*>(txt)[3] == '2', "bad advertised metadata");
    advertised_port = ntohs(port);
    return create(ref, [=, calls = 0](DNSServiceRef handle) mutable {
        if (scenario == 11)
            return;
        ++calls;
        callback(handle, 0, (scenario == 8 || calls > 1) ? kDNSServiceErr_Unknown : 0, "Laptop (2)",
                 type, domain, context);
        if (scenario == 9 && calls == 1) {
            const char notification = 'x';
            require(write(handle->descriptors[1], &notification, 1) == 1, "pipe write failed");
        }
    });
}
} // extern "C"
int main() {
    try {
        require(beam::is_discovery_name("My Laptop"), "valid display name rejected");
        for (const auto& name : {"", " leading", "trailing ", "a\nname", "a\tname"})
            require(!beam::is_discovery_name(name), "unsafe name accepted");
        require(!beam::is_discovery_name(std::string(64, 'a')), "overlong name accepted");
        beam::DiscoveryOptions options;
        options.timeout = std::chrono::milliseconds(80);
        const auto devices = beam::discover_devices(options);
        require(devices.size() == 2 && resolves == 3,
                "duplicate/interface/removal handling failed");
        for (const auto& device : devices) {
            require(device.name == "Laptop" && device.hostname == "laptop.local.", "bad metadata");
            require(device.endpoints.size() == 2, "bad address filtering/deduplication");
            require(device.endpoints[0].host == "192.168.1.50" && device.endpoints[0].port == 4269,
                    "IPv4 endpoint or network byte order failed");
            require(device.endpoints[1].host == "fe80::1%" + std::to_string(device.interface_index),
                    "missing IPv6 scope");
        }
        require(live == 0, "browse leaked references");
        for (scenario = 1; scenario <= 4; ++scenario) {
            require(beam::discover_devices(options).empty(), "unsafe/incompatible record accepted");
            require(live == 0, "invalid record leaked references");
        }
        for (scenario = 5; scenario <= 6; ++scenario)
            expect_error<beam::DiscoveryError>([&] { (void)beam::discover_devices(options); });
        scenario = 7;
        resolves = 0;
        require(beam::discover_devices(options).size() == 128 && resolves == 128,
                "unbounded services");
        require(live == 0, "flood leaked references");
        scenario = 0;
        {
            beam::ReceiverAdvertisement advertisement(4269, "Laptop", options);
            require(advertisement.name() == "Laptop (2)" && advertised_port == 4269,
                    "registration collision name/port failed");
            advertisement.check();
        }
        require(live == 0, "registration worker/reference leaked");
        scenario = 8;
        expect_error<beam::DiscoveryError>(
            [&] { beam::ReceiverAdvertisement advertisement(4269); });
        scenario = 9;
        {
            beam::ReceiverAdvertisement advertisement(4269, "Laptop", options);
            bool failed = false;
            for (int attempt = 0; attempt < 30 && !failed; ++attempt) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                try {
                    advertisement.check();
                } catch (const beam::DiscoveryError&) {
                    failed = true;
                }
            }
            require(failed, "asynchronous daemon failure lost");
        }
        require(live == 0, "failed worker/reference leaked");
        scenario = 10;
        for (const auto& device : beam::discover_devices(options))
            require(device.endpoints.size() == 16, "unbounded address list");
        scenario = 11;
        expect_error<beam::DiscoveryError>(
            [&] { beam::ReceiverAdvertisement advertisement(4269, "Laptop", options); });
        scenario = 0;
        expect_error<std::invalid_argument>([] { beam::ReceiverAdvertisement advertisement(0); });
        expect_error<std::invalid_argument>(
            [] { beam::ReceiverAdvertisement advertisement(4269, "bad\nname"); });
        options.timeout = std::chrono::milliseconds(0);
        expect_error<std::invalid_argument>([&] { (void)beam::discover_devices(options); });
        options.timeout = std::chrono::seconds(1);
        std::stop_source stop;
        stop.request_stop();
        options.stop_token = stop.get_token();
        expect_error<beam::DiscoveryError>([&] { (void)beam::discover_devices(options); });
        expect_error<beam::DiscoveryError>(
            [&] { beam::ReceiverAdvertisement advertisement(4269, "Laptop", options); });
        options.stop_token = {};
        int calls = 0;
        options.should_cancel = [&] { return ++calls > 3; };
        const auto start = std::chrono::steady_clock::now();
        expect_error<beam::DiscoveryError>([&] { (void)beam::discover_devices(options); });
        require(std::chrono::steady_clock::now() - start < std::chrono::milliseconds(300),
                "cancellation was not prompt");
        options.should_cancel = []() -> bool { throw std::logic_error("predicate failed"); };
        expect_error<std::logic_error>([&] { (void)beam::discover_devices(options); });
        std::cout
            << "Discovery callback, validation, bounds, cancellation, and lifecycle tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
