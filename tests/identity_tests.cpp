#include "beam/identity.hpp"
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <openssl/pem.h>
#include <sstream>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>

namespace {
void require(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}
template <class Operation> void rejects(Operation operation, const char* message) {
    bool failed = false;
    try {
        operation();
    } catch (const std::exception&) {
        failed = true;
    }
    require(failed, message);
}
struct Workspace {
    std::filesystem::path path;
    Workspace() {
        auto name = (std::filesystem::canonical(std::filesystem::temp_directory_path()) /
                     "beam-identity-XXXXXX")
                        .string();
        require(mkdtemp(name.data()) != nullptr, "create identity workspace failed");
        path = name;
    }
    ~Workspace() { std::filesystem::remove_all(path); }
};
void save(const std::filesystem::path& path, const std::string& text) {
    std::ofstream stream(path);
    stream << text;
    require(stream.good(), "save test certificate failed");
}
struct CertificateVariant {
    std::string pem;
    std::string fingerprint;
};
CertificateVariant altered_certificate(const std::string& pem,
                                       const std::filesystem::path& key_path,
                                       const std::string& mode, std::size_t padding_size = 0) {
    std::unique_ptr<BIO, decltype(&BIO_free)> input(
        BIO_new_mem_buf(pem.data(), static_cast<int>(pem.size())), BIO_free);
    std::unique_ptr<X509, decltype(&X509_free)> certificate(
        PEM_read_bio_X509(input.get(), nullptr, nullptr, nullptr), X509_free);
    std::unique_ptr<BIO, decltype(&BIO_free)> key_input(BIO_new_file(key_path.c_str(), "r"),
                                                        BIO_free);
    std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> key(
        PEM_read_bio_PrivateKey(key_input.get(), nullptr, nullptr, nullptr), EVP_PKEY_free);
    require(certificate && key, "load certificate fixture failed");
    if (mode == "padding") {
        const std::string padding(padding_size, 'a');
        std::unique_ptr<ASN1_OCTET_STRING, decltype(&ASN1_OCTET_STRING_free)> value(
            ASN1_OCTET_STRING_new(), ASN1_OCTET_STRING_free);
        std::unique_ptr<ASN1_OBJECT, decltype(&ASN1_OBJECT_free)> object(
            OBJ_txt2obj("1.3.6.1.4.1.55555.1", 1), ASN1_OBJECT_free);
        require(value && object &&
                    ASN1_OCTET_STRING_set(value.get(),
                                          reinterpret_cast<const unsigned char*>(padding.data()),
                                          static_cast<int>(padding.size())) == 1,
                "create large certificate fixture failed");
        std::unique_ptr<X509_EXTENSION, decltype(&X509_EXTENSION_free)> extension(
            X509_EXTENSION_create_by_OBJ(nullptr, object.get(), 0, value.get()),
            X509_EXTENSION_free);
        require(extension && X509_add_ext(certificate.get(), extension.get(), -1) == 1,
                "add fixture extension failed");
    }
    if (mode == "expired") {
        require(X509_gmtime_adj(X509_getm_notBefore(certificate.get()), -3600) &&
                    X509_gmtime_adj(X509_getm_notAfter(certificate.get()), -60),
                "set expired fixture dates failed");
    } else if (mode == "future") {
        require(X509_gmtime_adj(X509_getm_notBefore(certificate.get()), 3600) &&
                    X509_gmtime_adj(X509_getm_notAfter(certificate.get()), 7200),
                "set future fixture dates failed");
    } else if (mode == "invalid-date") {
        require(ASN1_STRING_set(X509_getm_notBefore(certificate.get()), "invalid", 7) == 1,
                "set malformed fixture date failed");
    }
    require(X509_sign(certificate.get(), key.get(), EVP_sha256()) > 0,
            "sign certificate fixture failed");
    if (mode == "bad-signature")
        require(ASN1_INTEGER_set(X509_get_serialNumber(certificate.get()), 99) == 1,
                "alter signed fixture failed");
    std::unique_ptr<BIO, decltype(&BIO_free)> output(BIO_new(BIO_s_mem()), BIO_free);
    require(PEM_write_bio_X509(output.get(), certificate.get()) == 1,
            "encode certificate fixture failed");
    char* data = nullptr;
    const auto size = BIO_get_mem_data(output.get(), &data);
    unsigned char digest[EVP_MAX_MD_SIZE]{};
    unsigned length = 0;
    require(X509_digest(certificate.get(), EVP_sha256(), digest, &length) == 1 && length == 32,
            "fingerprint fixture failed");
    std::ostringstream fingerprint;
    for (unsigned i = 0; i < length; ++i)
        fingerprint << std::hex << std::setfill('0') << std::setw(2)
                    << static_cast<unsigned>(digest[i]);
    return {{data, static_cast<std::size_t>(size)}, fingerprint.str()};
}
} // namespace
int main() {
    try {
        Workspace workspace;
        const auto alice_path = workspace.path / "alice";
        const auto bob_path = workspace.path / "bob";
        beam::DeviceIdentity alice, bob;
        {
            beam::IdentityStore store(alice_path, true);
            alice = store.initialize();
            require(store.initialize().fingerprint == alice.fingerprint,
                    "initialization changed persistent identity");
            require(alice.fingerprint.size() == 64 && alice.tls_name.size() == 37,
                    "unexpected identity encoding");
            require(alice.certificate_pem.find("PRIVATE KEY") == std::string::npos,
                    "public export leaked private key");
            rejects([&] { static_cast<void>(store.credentials()); }, "empty trust accepted");
        }
        {
            beam::IdentityStore store(bob_path, true);
            bob = store.initialize();
        }
        require(alice.fingerprint != bob.fingerprint, "devices share identity");
        const auto bob_certificate = workspace.path / "bob.pem";
        save(bob_certificate, bob.certificate_pem);
        struct stat status{};
        require(stat(alice_path.c_str(), &status) == 0 && (status.st_mode & 0777) == 0700,
                "state directory permissions incorrect");
        require(stat((alice_path / "identity.pem").c_str(), &status) == 0 &&
                    (status.st_mode & 0777) == 0600,
                "identity permissions incorrect");
        {
            beam::IdentityStore store(alice_path);
            require(store.identity().fingerprint == alice.fingerprint, "identity did not persist");
            rejects([&] { store.pair("bob", bob_certificate, std::string(64, '0')); },
                    "mismatched fingerprint accepted");
            require(store.peers().empty(), "failed pairing changed trust");
            rejects([&] { store.pair("../bob", bob_certificate, bob.fingerprint); },
                    "peer traversal accepted");
            rejects([&] { store.pair("Bob", bob_certificate, bob.fingerprint); },
                    "case ambiguous name accepted");
            rejects([&] { store.pair("bob", bob_certificate, "short"); },
                    "short fingerprint accepted");
            rejects([&] { store.pair("bob", bob_certificate, bob.fingerprint, "host:4269"); },
                    "DNS endpoint accepted");
            rejects([&] { store.pair("bob", bob_certificate, bob.fingerprint, "127.0.0.1:0"); },
                    "zero port accepted");
            for (const std::string endpoint :
                 {"127.0.0.1%2:4269", "[fe80::1%4294967296]:4269", "[fe80::1%0]:4269"})
                rejects([&] { store.pair("bob", bob_certificate, bob.fingerprint, endpoint); },
                        "invalid interface scope accepted");
            for (const std::string mode : {"expired", "future", "invalid-date", "bad-signature"}) {
                const auto altered =
                    altered_certificate(bob.certificate_pem, bob_path / "identity.pem", mode);
                save(workspace.path / "altered.pem", altered.pem);
                rejects(
                    [&] {
                        store.pair("invalid", workspace.path / "altered.pem", altered.fingerprint);
                    },
                    "invalid signed device certificate was paired");
                require(store.peers().empty(), "invalid certificate changed trust");
            }
            // A valid certificate can fit the import limit while its serialized
            // peer record (certificate plus endpoint) would exceed the store limit.
            std::size_t low = 0, high = 13000;
            while (low + 1 < high) {
                const auto middle = (low + high) / 2;
                const auto candidate = altered_certificate(
                    bob.certificate_pem, bob_path / "identity.pem", "padding", middle);
                if (candidate.pem.size() <= 16384)
                    low = middle;
                else
                    high = middle;
            }
            auto large =
                altered_certificate(bob.certificate_pem, bob_path / "identity.pem", "padding", low);
            while (large.pem.size() > 16384 && low > 0)
                large = altered_certificate(bob.certificate_pem, bob_path / "identity.pem",
                                            "padding", --low);
            require(large.pem.size() <= 16384 && large.pem.size() + 15 > 16384,
                    "certificate fixture did not reach the peer record boundary");
            save(workspace.path / "boundary.pem", large.pem);
            rejects(
                [&] {
                    store.pair("boundary", workspace.path / "boundary.pem", large.fingerprint,
                               "127.0.0.1:4269");
                },
                "oversized serialized peer record accepted");
            require(store.peers().empty() && !std::filesystem::exists(alice_path / "boundary.peer"),
                    "oversized record was published");
            const auto peer = store.pair("bob", bob_certificate, bob.fingerprint, "127.0.0.1:4269");
            require(peer.identity.fingerprint == bob.fingerprint && store.peers().size() == 1,
                    "pairing did not persist");
            require(store.peers().size() == 1, "directory scans share an exhausted cursor");
            rejects([&] { store.pair("bob", bob_certificate, bob.fingerprint); },
                    "existing name silently replaced");
            rejects([&] { store.pair("other", bob_certificate, bob.fingerprint); },
                    "duplicate identity accepted");
            save(workspace.path / "alice.pem", alice.certificate_pem);
            rejects([&] { store.pair("self", workspace.path / "alice.pem", alice.fingerprint); },
                    "self pairing accepted");
            store.set_endpoint("bob", "[::1]:5270");
            require(store.peer("bob").endpoint == "[::1]:5270" &&
                        store.peer("bob").identity.fingerprint == bob.fingerprint,
                    "endpoint update changed identity");
            const auto credentials = store.credentials("bob");
            require(credentials.ca_certificate.empty() &&
                        credentials.pinned_certificates.size() == 1 &&
                        credentials.pinned_certificates[0] == bob.certificate_pem,
                    "credentials did not restrict trust to selected identity");
            std::filesystem::rename(alice_path / "identity.pem", alice_path / "saved-identity.pem");
            rejects([&] { static_cast<void>(store.initialize()); },
                    "missing identity in a paired store was regenerated");
            require(!std::filesystem::exists(alice_path / "identity.pem"),
                    "failed initialization created a replacement identity");
            std::filesystem::rename(alice_path / "saved-identity.pem", alice_path / "identity.pem");
        }
        {
            beam::IdentityStore store(alice_path);
            require(store.peer("bob").endpoint == "[::1]:5270", "peer endpoint did not persist");
            require(store.credentials().pinned_certificates.size() == 1, "receiver trust missing");
            store.unpair("bob");
            require(store.peers().empty(), "unpair did not remove trust");
            rejects([&] { static_cast<void>(store.credentials()); },
                    "revoked peer remained trusted");
            rejects([&] { store.unpair("missing"); }, "missing peer silently removed");
        }
        std::filesystem::create_symlink(bob_path, workspace.path / "linked-state");
        rejects([&] { beam::IdentityStore store(workspace.path / "linked-state"); },
                "symlink state directory accepted");
        chmod(alice_path.c_str(), 0755);
        rejects([&] { beam::IdentityStore store(alice_path); }, "public state directory accepted");
        chmod(alice_path.c_str(), 0700);
        chmod((alice_path / "identity.pem").c_str(), 0644);
        {
            beam::IdentityStore store(alice_path);
            rejects([&] { static_cast<void>(store.identity()); }, "public private key accepted");
        }
        chmod((alice_path / "identity.pem").c_str(), 0600);
        std::filesystem::create_hard_link(alice_path / "identity.pem",
                                          workspace.path / "identity-link");
        {
            beam::IdentityStore store(alice_path);
            rejects([&] { static_cast<void>(store.identity()); },
                    "hardlinked private key accepted");
        }
        std::filesystem::remove(workspace.path / "identity-link");
        {
            beam::IdentityStore store(alice_path);
            rejects([&] { store.pair("private", bob_path / "identity.pem", bob.fingerprint); },
                    "private credential import accepted");
            save(workspace.path / "broken.pem", "garbage");
            rejects([&] { store.pair("broken", workspace.path / "broken.pem", bob.fingerprint); },
                    "malformed certificate accepted");
            save(workspace.path / "oversized.pem", std::string(20000, 'a'));
            rejects([&] { store.pair("large", workspace.path / "oversized.pem", bob.fingerprint); },
                    "unbounded certificate accepted");
            std::filesystem::create_symlink(bob_certificate, workspace.path / "linked.pem");
            rejects([&] { store.pair("link", workspace.path / "linked.pem", bob.fingerprint); },
                    "symlink certificate accepted");
            std::filesystem::create_symlink(bob_certificate, alice_path / "bob.peer");
            rejects([&] { static_cast<void>(store.credentials()); }, "symlink peer accepted");
            std::filesystem::remove(alice_path / "bob.peer");
            save(alice_path / "bob.peer", "invalid peer record");
            chmod((alice_path / "bob.peer").c_str(), 0600);
            rejects([&] { static_cast<void>(store.peers()); }, "corrupt trust record accepted");
            store.unpair("bob");
            require(store.peers().empty(), "corrupt peer could not be removed");
            save(alice_path / "identity.pem", "broken");
            chmod((alice_path / "identity.pem").c_str(), 0600);
            rejects([&] { static_cast<void>(store.initialize()); },
                    "corrupt identity silently regenerated");
        }
        require(!beam::is_peer_name("") && !beam::is_peer_name(std::string(64, 'a')) &&
                    beam::is_peer_name("laptop-2") && beam::is_peer_name(std::string(63, 'a')),
                "peer alias bounds incorrect");
        std::cout << "Persistent identity, pairing, storage safety and revocation verified\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
