#include "beam/identity.hpp"
#include <openssl/pem.h>
#include <openssl/rand.h>
#include <openssl/x509v3.h>

#include <algorithm>
#include <arpa/inet.h>
#include <array>
#include <cerrno>
#include <charconv>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

namespace beam {
namespace {
constexpr std::size_t max_certificate = 16384;
constexpr std::size_t max_peers = 128;
struct File {
    int descriptor;
    explicit File(int fd, const std::string& context = "open identity store") : descriptor(fd) {
        if (fd < 0)
            throw std::runtime_error(context + ": " + std::strerror(errno));
    }
    ~File() { ::close(descriptor); }
    File(const File&) = delete;
    File& operator=(const File&) = delete;
};
using Certificate = std::unique_ptr<X509, decltype(&X509_free)>;
using Key = std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)>;
using Bio = std::unique_ptr<BIO, decltype(&BIO_free)>;
void checked(bool success, const char* message) {
    if (!success)
        throw std::runtime_error(message);
}
std::string hex(const unsigned char* bytes, std::size_t count) {
    constexpr char digits[] = "0123456789abcdef";
    std::string result;
    result.reserve(count * 2);
    for (std::size_t i = 0; i < count; ++i) {
        result += digits[bytes[i] >> 4];
        result += digits[bytes[i] & 15];
    }
    return result;
}
std::string bio_text(BIO* bio) {
    char* data = nullptr;
    const auto size = BIO_get_mem_data(bio, &data);
    checked(size > 0 && data, "encode device identity failed");
    return {data, static_cast<std::size_t>(size)};
}
Certificate parse_certificate(const std::string& pem) {
    if (pem.empty() || pem.size() > max_certificate)
        throw std::runtime_error("invalid or oversized device certificate");
    Bio input(BIO_new_mem_buf(pem.data(), static_cast<int>(pem.size())), BIO_free);
    checked(input != nullptr, "allocate certificate reader failed");
    Certificate certificate(PEM_read_bio_X509(input.get(), nullptr, nullptr, nullptr), X509_free);
    checked(certificate != nullptr, "invalid device certificate");
    return certificate;
}
DeviceIdentity describe(X509* certificate, bool check_validity = true) {
    std::array<unsigned char, EVP_MAX_MD_SIZE> digest{};
    unsigned size = 0;
    checked(X509_digest(certificate, EVP_sha256(), digest.data(), &size) == 1 && size == 32,
            "fingerprint device certificate failed");
    const auto* subject = X509_get_subject_name(certificate);
    const int index = X509_NAME_get_index_by_NID(subject, NID_commonName, -1);
    checked(index >= 0 && X509_NAME_get_index_by_NID(subject, NID_commonName, index) < 0,
            "device certificate must have one Beam identity name");
    const auto* common_name = X509_NAME_ENTRY_get_data(X509_NAME_get_entry(subject, index));
    checked(common_name && ASN1_STRING_length(common_name) == 37,
            "device certificate must have a Beam identity name");
    const std::string name(reinterpret_cast<const char*>(ASN1_STRING_get0_data(common_name)), 37);
    checked(
        name.starts_with("beam-") &&
            std::all_of(name.begin() + 5, name.end(),
                        [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); }) &&
            X509_check_host(certificate, name.c_str(), name.size(),
                            X509_CHECK_FLAG_NEVER_CHECK_SUBJECT, nullptr) == 1,
        "device certificate has an invalid TLS identity name");
    checked(
        X509_check_ca(certificate) == 0 &&
            X509_check_purpose(certificate, X509_PURPOSE_SSL_CLIENT, 0) == 1 &&
            X509_check_purpose(certificate, X509_PURPOSE_SSL_SERVER, 0) == 1,
        "device certificate must support client and server authentication, without CA authority");
    Key key(X509_get_pubkey(certificate), EVP_PKEY_free);
    checked(key && EVP_PKEY_security_bits(key.get()) >= 128 &&
                X509_NAME_cmp(X509_get_subject_name(certificate),
                              X509_get_issuer_name(certificate)) == 0 &&
                X509_verify(certificate, key.get()) == 1,
            "device certificate must be self-signed");
    if (check_validity) {
        std::unique_ptr<ASN1_TIME, decltype(&ASN1_TIME_free)> now(
            ASN1_TIME_set(nullptr, std::time(nullptr)), ASN1_TIME_free);
        const auto begins =
            now ? ASN1_TIME_compare(X509_get0_notBefore(certificate), now.get()) : -2;
        checked(now && (begins == -1 || begins == 0) &&
                    ASN1_TIME_compare(X509_get0_notAfter(certificate), now.get()) > 0,
                "device certificate is not currently valid");
    }
    Bio output(BIO_new(BIO_s_mem()), BIO_free);
    checked(output && PEM_write_bio_X509(output.get(), certificate) == 1,
            "encode device certificate failed");
    return {hex(digest.data(), size), name, bio_text(output.get())};
}
void extension(X509* certificate, int nid, const std::string& value) {
    X509V3_CTX context{};
    X509V3_set_ctx(&context, certificate, certificate, nullptr, nullptr, 0);
    std::unique_ptr<X509_EXTENSION, decltype(&X509_EXTENSION_free)> ext(
        X509V3_EXT_conf_nid(nullptr, &context, nid, value.c_str()), X509_EXTENSION_free);
    checked(ext && X509_add_ext(certificate, ext.get(), -1) == 1,
            "add device certificate extension failed");
}
std::string generate_identity() {
    std::unique_ptr<EVP_PKEY_CTX, decltype(&EVP_PKEY_CTX_free)> context(
        EVP_PKEY_CTX_new_id(EVP_PKEY_EC, nullptr), EVP_PKEY_CTX_free);
    checked(context && EVP_PKEY_keygen_init(context.get()) == 1 &&
                EVP_PKEY_CTX_set_ec_paramgen_curve_nid(context.get(), NID_X9_62_prime256v1) == 1,
            "initialize device key generation failed");
    EVP_PKEY* generated = nullptr;
    checked(EVP_PKEY_keygen(context.get(), &generated) == 1, "generate device key failed");
    Key key(generated, EVP_PKEY_free);
    Certificate certificate(X509_new(), X509_free);
    std::array<unsigned char, 16> random{};
    checked(RAND_bytes(random.data(), static_cast<int>(random.size())) == 1,
            "generate device identity name failed");
    const auto name = "beam-" + hex(random.data(), random.size());
    checked(certificate && X509_set_version(certificate.get(), 2) == 1 &&
                ASN1_INTEGER_set(X509_get_serialNumber(certificate.get()), 1) == 1 &&
                X509_gmtime_adj(X509_getm_notBefore(certificate.get()), -300) &&
                X509_gmtime_adj(X509_getm_notAfter(certificate.get()), 10L * 365 * 24 * 60 * 60) &&
                X509_set_pubkey(certificate.get(), key.get()) == 1,
            "create device certificate failed");
    std::unique_ptr<X509_NAME, decltype(&X509_NAME_free)> subject(X509_NAME_new(), X509_NAME_free);
    checked(subject != nullptr, "allocate certificate name failed");
    checked(X509_NAME_add_entry_by_txt(subject.get(), "CN", MBSTRING_ASC,
                                       reinterpret_cast<const unsigned char*>(name.c_str()), -1, -1,
                                       0) == 1 &&
                X509_set_subject_name(certificate.get(), subject.get()) == 1 &&
                X509_set_issuer_name(certificate.get(), subject.get()) == 1,
            "set device certificate name failed");
    extension(certificate.get(), NID_basic_constraints, "critical,CA:FALSE");
    extension(certificate.get(), NID_key_usage, "critical,digitalSignature");
    extension(certificate.get(), NID_ext_key_usage, "serverAuth,clientAuth");
    extension(certificate.get(), NID_subject_alt_name, "DNS:" + name);
    checked(X509_sign(certificate.get(), key.get(), EVP_sha256()) > 0,
            "sign device certificate failed");
    Bio output(BIO_new(BIO_s_mem()), BIO_free);
    checked(output && PEM_write_bio_X509(output.get(), certificate.get()) == 1 &&
                PEM_write_bio_PrivateKey(output.get(), key.get(), nullptr, nullptr, 0, nullptr,
                                         nullptr) == 1,
            "encode device credentials failed");
    return bio_text(output.get());
}
void validate_file(int fd, bool private_file) {
    struct stat status{};
    checked(
        fstat(fd, &status) == 0 && S_ISREG(status.st_mode) && status.st_nlink == 1 &&
            (!private_file || (status.st_uid == getuid() && (status.st_mode & 0777) == 0600)),
        "identity store files must be owned by you, regular, unlinked elsewhere, and mode 0600");
}
std::string read_file(int fd, bool private_file) {
    validate_file(fd, private_file);
    std::string text;
    std::array<char, 4096> buffer{};
    for (;;) {
        const auto count = read(fd, buffer.data(), buffer.size());
        if (count < 0 && errno == EINTR)
            continue;
        checked(count >= 0, "read identity store file failed");
        if (!count)
            break;
        text.append(buffer.data(), static_cast<std::size_t>(count));
        checked(text.size() <= max_certificate, "identity store file is too large");
    }
    return text;
}
void validate_endpoint(const std::string& text) {
    if (text.empty())
        return;
    checked(text.size() <= 128 && text.find_first_of("\r\n\t ") == std::string::npos,
            "invalid stored endpoint");
    const auto endpoint = parse_endpoint(text);
    // Use the same numeric-address parser as QUIC, without accepting hostnames.
    const auto scope = endpoint.host.find('%');
    const auto address = endpoint.host.substr(0, scope);
    std::array<unsigned char, 16> bytes{};
    const bool ipv6 = inet_pton(AF_INET6, address.c_str(), bytes.data()) == 1;
    checked(ipv6 || (scope == std::string::npos &&
                     inet_pton(AF_INET, address.c_str(), bytes.data()) == 1),
            "stored endpoint must contain a numeric IPv4 or IPv6 address");
    if (scope != std::string::npos) {
        const auto index_text = std::string_view(endpoint.host).substr(scope + 1);
        std::uint32_t index = 0;
        const auto [end, error] =
            std::from_chars(index_text.data(), index_text.data() + index_text.size(), index);
        checked(ipv6 && index > 0 && error == std::errc{} &&
                    end == index_text.data() + index_text.size(),
                "IPv6 scope must be a nonzero 32-bit numeric interface index");
    }
}
int open_directory(const std::filesystem::path& path, bool create) {
    if (path.empty())
        throw std::invalid_argument("state directory must not be empty");
    const auto absolute = std::filesystem::absolute(path).lexically_normal();
    File current(open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC));
    int descriptor = dup(current.descriptor);
    checked(descriptor >= 0, "open state directory failed");
    try {
        for (const auto& component : absolute.relative_path()) {
            const auto name = component.string();
            if (create && mkdirat(descriptor, name.c_str(), 0700) != 0 && errno != EEXIST)
                throw std::runtime_error("create private state directory failed");
            const int next =
                openat(descriptor, name.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
            checked(next >= 0,
                    "state directory missing or contains a symlink; run beam identity init");
            close(descriptor);
            descriptor = next;
        }
        struct stat status{};
        checked(fstat(descriptor, &status) == 0 && status.st_uid == getuid() &&
                    (status.st_mode & 0777) == 0700,
                "state directory must be owned by you and mode 0700");
        return descriptor;
    } catch (...) {
        close(descriptor);
        throw;
    }
}
int open_lock(int directory) {
    // Create once, or open the existing inode. Never replace a lock another
    // process may already hold, and reject symlinks in either branch.
    const int created =
        openat(directory, ".lock", O_RDWR | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (created >= 0 || errno != EEXIST)
        return created;
    return openat(directory, ".lock", O_RDWR | O_NOFOLLOW | O_CLOEXEC);
}
} // namespace
struct IdentityStore::Impl {
    std::filesystem::path directory;
    File root;
    File lock;
    Impl(const std::filesystem::path& path, bool create)
        : directory(std::filesystem::absolute(path).lexically_normal()),
          root(open_directory(path, create)), lock(open_lock(root.descriptor), "open state lock") {
        validate_file(lock.descriptor, true);
        checked(flock(lock.descriptor, LOCK_EX) == 0, "lock identity store failed");
    }
    std::string read(const std::string& name) const {
        File file(
            openat(root.descriptor, name.c_str(), O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC),
            "read " + name);
        return read_file(file.descriptor, true);
    }
    bool exists(const std::string& name) const {
        struct stat status{};
        if (fstatat(root.descriptor, name.c_str(), &status, AT_SYMLINK_NOFOLLOW) == 0)
            return true;
        checked(errno == ENOENT, "inspect identity store file failed");
        return false;
    }
    void write(const std::string& name, const std::string& contents, bool replace = false) const {
        checked(contents.size() <= max_certificate, "identity store record is too large");
        std::array<unsigned char, 16> random{};
        checked(RAND_bytes(random.data(), static_cast<int>(random.size())) == 1,
                "generate state temporary name failed");
        const auto temporary = ".pending-" + hex(random.data(), random.size());
        File file(openat(root.descriptor, temporary.c_str(),
                         O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600));
        try {
            std::size_t offset = 0;
            while (offset < contents.size()) {
                const auto count =
                    ::write(file.descriptor, contents.data() + offset, contents.size() - offset);
                if (count < 0 && errno == EINTR)
                    continue;
                checked(count > 0, "write identity store file failed");
                offset += static_cast<std::size_t>(count);
            }
            checked(fsync(file.descriptor) == 0, "flush identity store file failed");
            if (replace) {
                checked(renameat(root.descriptor, temporary.c_str(), root.descriptor,
                                 name.c_str()) == 0,
                        "replace peer record failed");
            } else {
                checked(
                    linkat(root.descriptor, temporary.c_str(), root.descriptor, name.c_str(), 0) ==
                        0,
                    "identity or peer already exists; remove peer explicitly before replacing it");
                checked(unlinkat(root.descriptor, temporary.c_str(), 0) == 0,
                        "remove state temporary failed");
            }
            checked(fsync(root.descriptor) == 0, "flush identity store directory failed");
        } catch (...) {
            unlinkat(root.descriptor, temporary.c_str(), 0);
            throw;
        }
    }
};
std::filesystem::path default_state_directory() {
    if (const auto* config = std::getenv("XDG_CONFIG_HOME"); config && *config) {
        if (!std::filesystem::path(config).is_absolute())
            throw std::runtime_error("XDG_CONFIG_HOME must be an absolute path");
        return std::filesystem::path(config) / "beam";
    }
    const auto* home = std::getenv("HOME");
    if (!home || !*home || !std::filesystem::path(home).is_absolute())
        throw std::runtime_error("HOME must be an absolute path; use --state-dir");
    return std::filesystem::path(home) / ".config/beam";
}
bool is_peer_name(std::string_view name) noexcept {
    return !name.empty() && name.size() <= 63 && name.front() >= 'a' && name.front() <= 'z' &&
           std::all_of(name.begin(), name.end(), [](char c) {
               return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
           });
}
IdentityStore::IdentityStore(const std::filesystem::path& directory, bool create)
    : implementation(std::make_unique<Impl>(directory, create)) {}
IdentityStore::~IdentityStore() = default;
DeviceIdentity IdentityStore::initialize() {
    if (!implementation->exists("identity.pem")) {
        checked(peers().empty(), "device identity missing from a paired store; restore it or "
                                 "explicitly start with a new store");
        implementation->write("identity.pem", generate_identity());
    }
    return identity();
}
DeviceIdentity IdentityStore::identity() const {
    checked(implementation->exists("identity.pem"),
            "device identity missing; run beam identity init");
    const auto pem = implementation->read("identity.pem");
    auto certificate = parse_certificate(pem);
    Bio input(BIO_new_mem_buf(pem.data(), static_cast<int>(pem.size())), BIO_free);
    checked(input != nullptr, "allocate device key reader failed");
    Key key(PEM_read_bio_PrivateKey(
                input.get(), nullptr, +[](char*, int, int, void*) { return 0; }, nullptr),
            EVP_PKEY_free);
    checked(key && X509_check_private_key(certificate.get(), key.get()) == 1,
            "device private key does not match its certificate");
    return describe(certificate.get());
}
TrustedPeer IdentityStore::peer(const std::string& name) const {
    if (!is_peer_name(name))
        throw std::invalid_argument(
            "peer name must be 1-63 lowercase letters, digits, '-' or '_', starting with a letter");
    checked(implementation->exists(name + ".peer"), ("unknown trusted peer: " + name).c_str());
    const auto text = implementation->read(name + ".peer");
    const auto newline = text.find('\n');
    checked(newline != std::string::npos, "invalid peer record");
    const auto endpoint = text.substr(0, newline);
    validate_endpoint(endpoint);
    auto certificate = parse_certificate(text.substr(newline + 1));
    return {name, endpoint, describe(certificate.get(), false)};
}
std::vector<TrustedPeer> IdentityStore::peers() const {
    // openat creates a fresh directory cursor; dup would share a previous scan's offset.
    File cursor(openat(implementation->root.descriptor, ".", O_RDONLY | O_DIRECTORY | O_CLOEXEC));
    const int scan = dup(cursor.descriptor);
    checked(scan >= 0, "scan trusted peers failed");
    DIR* raw = fdopendir(scan);
    if (!raw) {
        close(scan);
        throw std::runtime_error("scan trusted peers failed");
    }
    std::unique_ptr<DIR, decltype(&closedir)> directory(raw, closedir);
    std::vector<TrustedPeer> result;
    for (;;) {
        errno = 0;
        const auto* entry = readdir(directory.get());
        if (!entry) {
            checked(errno == 0, "read peer directory failed");
            break;
        }
        const std::string name(entry->d_name);
        if (!name.ends_with(".peer"))
            continue;
        checked(result.size() < max_peers, "too many trusted peers");
        result.push_back(peer(name.substr(0, name.size() - 5)));
    }
    std::sort(result.begin(), result.end(),
              [](const auto& a, const auto& b) { return a.name < b.name; });
    return result;
}
TrustedPeer IdentityStore::pair(const std::string& name,
                                const std::filesystem::path& certificate_path,
                                const std::string& verified_fingerprint,
                                const std::string& endpoint) {
    if (!is_peer_name(name))
        throw std::invalid_argument("invalid peer name; use lowercase letters, digits, '-' or '_'");
    if (verified_fingerprint.size() != 64 ||
        !std::all_of(verified_fingerprint.begin(), verified_fingerprint.end(),
                     [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); }))
        throw std::invalid_argument(
            "fingerprint must be the full 64-character lowercase SHA-256 value");
    validate_endpoint(endpoint);
    File input(open(certificate_path.c_str(), O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC));
    const auto pem = read_file(input.descriptor, false);
    checked(pem.find("PRIVATE KEY") == std::string::npos,
            "pair with an exported public certificate, never a private key");
    auto certificate = parse_certificate(pem);
    const auto remote = describe(certificate.get());
    checked(remote.fingerprint == verified_fingerprint,
            "peer fingerprint mismatch; trust was not changed");
    checked(remote.fingerprint != identity().fingerprint, "cannot pair this device with itself");
    const auto current = peers();
    checked(current.size() < max_peers, "trusted peer limit reached");
    for (const auto& saved : current)
        checked(saved.name != name && saved.identity.fingerprint != remote.fingerprint,
                "peer name or identity already paired; unpair explicitly before replacing it");
    implementation->write(name + ".peer", endpoint + "\n" + remote.certificate_pem);
    return {name, endpoint, remote};
}
void IdentityStore::unpair(const std::string& name) {
    if (!is_peer_name(name))
        throw std::invalid_argument("invalid peer name");
    File file(openat(implementation->root.descriptor, (name + ".peer").c_str(),
                     O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC));
    validate_file(file.descriptor, true);
    checked(unlinkat(implementation->root.descriptor, (name + ".peer").c_str(), 0) == 0 &&
                fsync(implementation->root.descriptor) == 0,
            "remove trusted peer failed");
}
void IdentityStore::set_endpoint(const std::string& name, const std::string& endpoint) {
    if (endpoint.empty())
        throw std::invalid_argument("endpoint must not be empty");
    validate_endpoint(endpoint);
    const auto saved = peer(name);
    implementation->write(name + ".peer", endpoint + "\n" + saved.identity.certificate_pem, true);
}
Credentials IdentityStore::credentials(const std::string& name) const {
    static_cast<void>(identity());
    Credentials result{
        implementation->directory / "identity.pem", implementation->directory / "identity.pem", {}};
    const auto selected = name.empty() ? peers() : std::vector{peer(name)};
    checked(!selected.empty(), "no trusted peers; pair a device before transferring");
    for (const auto& saved : selected)
        result.pinned_certificates.push_back(saved.identity.certificate_pem);
    return result;
}
} // namespace beam
