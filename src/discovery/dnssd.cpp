#include "beam/discovery.hpp"
#include <algorithm>
#include <arpa/inet.h>
#include <array>
#include <cerrno>
#include <cstring>
#include <dns_sd.h>
#include <exception>
#include <mutex>
#include <poll.h>
#include <span>
#include <thread>
#include <tuple>

namespace beam {
bool is_discovery_name(std::string_view name) noexcept {
    return !name.empty() && name.size() <= 63 && name.front() != ' ' && name.back() != ' ' &&
           std::all_of(name.begin(), name.end(),
                       [](unsigned char c) { return c >= 32 && c <= 126; });
}
namespace {
constexpr const char* service_type = "_beam._udp";
constexpr const char* domain = "local.";
constexpr std::size_t max_services = 128;
constexpr std::size_t max_addresses = 16;
void validate(const DiscoveryOptions& options) {
    if (options.timeout <= std::chrono::milliseconds::zero() ||
        options.timeout > std::chrono::minutes(1))
        throw std::invalid_argument("discovery timeout must be positive and at most 60 seconds");
}
void check_cancel(const DiscoveryOptions& options) {
    if (options.stop_token.stop_requested() || (options.should_cancel && options.should_cancel()))
        throw DiscoveryError("LAN discovery cancelled", true);
}
void check_dns(DNSServiceErrorType error) {
    if (error != kDNSServiceErr_NoError)
        throw DiscoveryError("DNS-SD failed (code " + std::to_string(error) +
                             "); check the Bonjour/Avahi daemon and LAN multicast support");
}
struct Reference {
    DNSServiceRef value = nullptr;
    Reference() = default;
    Reference(const Reference&) = delete;
    Reference& operator=(const Reference&) = delete;
    ~Reference() { reset(); }
    void reset() {
        if (value)
            DNSServiceRefDeallocate(value);
        value = nullptr;
    }
};
// Wait only for ready descriptors; ProcessResult otherwise blocks indefinitely.
bool ready(DNSServiceRef ref, int timeout) {
    pollfd descriptor{DNSServiceRefSockFD(ref), POLLIN, 0};
    if (descriptor.fd < 0)
        throw DiscoveryError("DNS-SD returned an invalid socket");
    const int result = poll(&descriptor, 1, timeout);
    if (result < 0 && errno != EINTR)
        throw DiscoveryError("polling DNS-SD failed");
    if (descriptor.revents & (POLLERR | POLLHUP | POLLNVAL))
        throw DiscoveryError("DNS-SD daemon disconnected");
    return result > 0 && (descriptor.revents & POLLIN);
}
bool compatible(std::span<const unsigned char> txt) {
    if (txt.empty() || txt.size() > 512)
        return false;
    bool version = false;
    while (!txt.empty()) {
        const auto length = txt.front();
        txt = txt.subspan(1);
        if (length > txt.size())
            return false;
        const std::string_view field(reinterpret_cast<const char*>(txt.data()), length);
        const auto key = field.substr(0, field.find('='));
        if (key == "v" || key == "V") {
            if (version || field != "v=2")
                return false;
            version = true;
        }
        txt = txt.subspan(length);
    }
    return version;
}
bool local_hostname(std::string_view hostname) {
    if (hostname.size() > 253 || hostname.size() <= 7)
        return false;
    std::string folded(hostname);
    for (auto& c : folded) {
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c + ('a' - 'A'));
        if (!(c == '.' || c == '-' || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')))
            return false;
    }
    return folded.ends_with(".local.");
}
struct Browser;
struct Service {
    Browser& browser;
    DiscoveredDevice device;
    Reference resolve, ipv4, ipv6;
    std::uint16_t port = 0;
    bool removed = false, resolved = false;
    Service(Browser& owner, std::string name, std::uint32_t index)
        : browser(owner), device{std::move(name), {}, index, {}} {}
};
struct Browser {
    Reference browse;
    std::vector<std::unique_ptr<Service>> services;
    std::exception_ptr failure;
    template <typename Action> void callback(Action action) noexcept {
        try {
            action();
        } catch (...) {
            failure = std::current_exception();
        }
    }
    void check() {
        if (failure)
            std::rethrow_exception(failure);
    }
    void prune() {
        for (auto& service : services) {
            if (service->resolved)
                service->resolve.reset();
        }
        std::erase_if(services, [](const auto& service) { return service->removed; });
    }
};
void DNSSD_API address_reply(DNSServiceRef, DNSServiceFlags flags, std::uint32_t index,
                             DNSServiceErrorType error, const char*, std::uint16_t type,
                             std::uint16_t record_class, std::uint16_t length, const void* data,
                             std::uint32_t, void* context) noexcept {
    auto& service = *static_cast<Service*>(context);
    service.browser.callback([&] {
        if (error != kDNSServiceErr_NoError || service.removed || !data ||
            index != service.device.interface_index || record_class != kDNSServiceClass_IN)
            return;
        const bool ipv6 = type == kDNSServiceType_AAAA && length == 16;
        if (!ipv6 && !(type == kDNSServiceType_A && length == 4))
            return;
        std::array<unsigned char, 16> bytes{};
        std::memcpy(bytes.data(), data, length);
        // Do not display unspecified, multicast, or limited-broadcast destinations.
        if (ipv6 ? (std::all_of(bytes.begin(), bytes.end(), [](auto b) { return b == 0; }) ||
                    bytes[0] == 0xff)
                 : (bytes[0] == 0 || bytes[0] >= 224))
            return;
        std::array<char, INET6_ADDRSTRLEN> text{};
        if (!inet_ntop(ipv6 ? AF_INET6 : AF_INET, data, text.data(), text.size()))
            return;
        std::string host(text.data());
        if (ipv6 && bytes[0] == 0xfe && (bytes[1] & 0xc0) == 0x80)
            host += "%" + std::to_string(index);
        auto& endpoints = service.device.endpoints;
        const auto found =
            std::find_if(endpoints.begin(), endpoints.end(),
                         [&](const auto& endpoint) { return endpoint.host == host; });
        if (!(flags & kDNSServiceFlagsAdd)) {
            if (found != endpoints.end())
                endpoints.erase(found);
        } else if (found == endpoints.end() && endpoints.size() < max_addresses) {
            endpoints.push_back({std::move(host), service.port});
        }
    });
}
void DNSSD_API resolve_reply(DNSServiceRef, DNSServiceFlags, std::uint32_t index,
                             DNSServiceErrorType error, const char*, const char* hostname,
                             std::uint16_t port, std::uint16_t txt_length, const unsigned char* txt,
                             void* context) noexcept {
    auto& service = *static_cast<Service*>(context);
    service.browser.callback([&] {
        if (service.resolved)
            return;
        service.resolved = true;
        if (error != kDNSServiceErr_NoError || service.removed || !hostname || !txt || !port ||
            index != service.device.interface_index || !local_hostname(hostname) ||
            !compatible({txt, txt_length}))
            return;
        service.device.hostname = hostname;
        // Save the port independently of address callbacks.
        service.port = ntohs(port);
        check_dns(DNSServiceQueryRecord(&service.ipv4.value, 0, index, hostname, kDNSServiceType_A,
                                        kDNSServiceClass_IN, address_reply, &service));
        check_dns(DNSServiceQueryRecord(&service.ipv6.value, 0, index, hostname,
                                        kDNSServiceType_AAAA, kDNSServiceClass_IN, address_reply,
                                        &service));
    });
}
void DNSSD_API browse_reply(DNSServiceRef, DNSServiceFlags flags, std::uint32_t index,
                            DNSServiceErrorType error, const char* name, const char* type,
                            const char* reply_domain, void* context) noexcept {
    auto& browser = *static_cast<Browser*>(context);
    browser.callback([&] {
        check_dns(error);
        if (!name || !type || !reply_domain || !is_discovery_name(name) ||
            (std::string_view(type) != service_type && std::string_view(type) != "_beam._udp.") ||
            std::string_view(reply_domain) != domain || index == 0)
            return;
        const auto found = std::find_if(
            browser.services.begin(), browser.services.end(), [&](const auto& service) {
                return !service->removed && service->device.name == name &&
                       service->device.interface_index == index;
            });
        if (!(flags & kDNSServiceFlagsAdd)) {
            if (found != browser.services.end())
                (*found)->removed = true;
            return;
        }
        if (found != browser.services.end() || browser.services.size() >= max_services)
            return;
        auto service = std::make_unique<Service>(browser, name, index);
        check_dns(DNSServiceResolve(&service->resolve.value, 0, index, name, service_type, domain,
                                    resolve_reply, service.get()));
        browser.services.push_back(std::move(service));
    });
}
} // namespace
std::vector<DiscoveredDevice> discover_devices(const DiscoveryOptions& options) {
    validate(options);
    check_cancel(options);
    Browser browser;
    check_dns(DNSServiceBrowse(&browser.browse.value, 0, 0, service_type, domain, browse_reply,
                               &browser));
    const auto deadline = std::chrono::steady_clock::now() + options.timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        check_cancel(options);
        browser.prune();
        std::vector<DNSServiceRef> references{browser.browse.value};
        for (auto& service : browser.services)
            for (auto* ref : {&service->resolve, &service->ipv4, &service->ipv6})
                if (ref->value)
                    references.push_back(ref->value);
        std::vector<pollfd> descriptors;
        for (auto ref : references) {
            const auto fd = DNSServiceRefSockFD(ref);
            if (fd < 0)
                throw DiscoveryError("DNS-SD returned an invalid socket");
            descriptors.push_back({fd, POLLIN, 0});
        }
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now());
        const auto wait = static_cast<int>(std::clamp<std::int64_t>(remaining.count(), 0, 50));
        const auto count = poll(descriptors.data(), static_cast<nfds_t>(descriptors.size()), wait);
        if (count < 0 && errno != EINTR)
            throw DiscoveryError("polling DNS-SD failed");
        for (std::size_t i = 0; i < descriptors.size(); ++i) {
            if (descriptors[i].revents & (POLLERR | POLLHUP | POLLNVAL))
                throw DiscoveryError("DNS-SD daemon disconnected");
            if (descriptors[i].revents & POLLIN) {
                check_dns(DNSServiceProcessResult(references[i]));
                browser.check();
            }
        }
    }
    check_cancel(options);
    std::vector<DiscoveredDevice> devices;
    for (auto& service : browser.services) {
        if (!service->removed && !service->device.endpoints.empty()) {
            std::sort(service->device.endpoints.begin(), service->device.endpoints.end(),
                      [](const auto& a, const auto& b) { return a.host < b.host; });
            devices.push_back(std::move(service->device));
        }
    }
    std::sort(devices.begin(), devices.end(), [](const auto& a, const auto& b) {
        return std::tie(a.name, a.interface_index) < std::tie(b.name, b.interface_index);
    });
    return devices;
}
struct ReceiverAdvertisement::Impl {
    Reference registration;
    mutable std::mutex mutex;
    std::string registered_name;
    std::exception_ptr failure;
    bool registered = false;
    // Last member: joined before the registration or callback state is destroyed.
    std::jthread worker;
    static void DNSSD_API reply(DNSServiceRef, DNSServiceFlags, DNSServiceErrorType error,
                                const char* name, const char*, const char*,
                                void* context) noexcept {
        auto& self = *static_cast<Impl*>(context);
        try {
            check_dns(error);
            if (!name)
                throw DiscoveryError("DNS-SD returned no registered name");
            std::lock_guard lock(self.mutex);
            self.registered_name = name;
            self.registered = true;
        } catch (...) {
            std::lock_guard lock(self.mutex);
            self.failure = std::current_exception();
        }
    }
    void check() const {
        std::lock_guard lock(mutex);
        if (failure)
            std::rethrow_exception(failure);
    }
};
ReceiverAdvertisement::ReceiverAdvertisement(std::uint16_t port, const std::string& name,
                                             const DiscoveryOptions& options)
    : implementation(std::make_unique<Impl>()) {
    validate(options);
    if (!port || !is_discovery_name(name))
        throw std::invalid_argument(
            "discovery requires a port and a printable ASCII name of 1-63 bytes");
    check_cancel(options);
    const unsigned char txt[]{3, 'v', '=', '2'};
    auto& self = *implementation;
    check_dns(DNSServiceRegister(&self.registration.value, 0, 0, name.c_str(), service_type, domain,
                                 nullptr, htons(port), sizeof(txt), txt, Impl::reply, &self));
    const auto deadline = std::chrono::steady_clock::now() + options.timeout;
    while (!self.registered) {
        check_cancel(options);
        if (std::chrono::steady_clock::now() >= deadline)
            throw DiscoveryError("LAN advertisement registration timed out");
        if (ready(self.registration.value, 50))
            check_dns(DNSServiceProcessResult(self.registration.value));
        self.check();
    }
    self.worker = std::jthread([&self](std::stop_token stop) {
        try {
            while (!stop.stop_requested()) {
                if (ready(self.registration.value, 50))
                    check_dns(DNSServiceProcessResult(self.registration.value));
                self.check();
            }
        } catch (...) {
            std::lock_guard lock(self.mutex);
            self.failure = std::current_exception();
        }
    });
}
ReceiverAdvertisement::~ReceiverAdvertisement() = default;
std::string ReceiverAdvertisement::name() const {
    std::lock_guard lock(implementation->mutex);
    return implementation->registered_name;
}
void ReceiverAdvertisement::check() const { implementation->check(); }
} // namespace beam
