#include "files.hpp"
#include "beam/beam.hpp"
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>

namespace beam::transfer {
namespace {
[[noreturn]] void file_error(const char* operation) {
    const auto code =
        errno == EEXIST ? TransferErrorCode::destination_exists : TransferErrorCode::io;
    throw TransferError(code, std::string(operation) + ": " + std::strerror(errno));
}
void crypto_check(int result) {
    if (result != 1)
        throw TransferError(TransferErrorCode::io, "OpenSSL SHA-256 operation failed");
}
} // namespace
struct Hash::Impl {
    EVP_MD_CTX* context{EVP_MD_CTX_new()};
    Impl() {
        if (!context)
            throw TransferError(TransferErrorCode::io, "allocate SHA-256 context failed");
        if (EVP_DigestInit_ex(context, EVP_sha256(), nullptr) != 1) {
            EVP_MD_CTX_free(context);
            throw TransferError(TransferErrorCode::io, "initialize SHA-256 failed");
        }
    }
    ~Impl() { EVP_MD_CTX_free(context); }
};
Hash::Hash() : impl(std::make_unique<Impl>()) {}
Hash::~Hash() = default;
void Hash::update(std::span<const std::byte> bytes) {
    crypto_check(EVP_DigestUpdate(impl->context, bytes.data(), bytes.size()));
}
protocol::Digest Hash::finish() {
    protocol::Digest digest{};
    unsigned size = 0;
    crypto_check(
        EVP_DigestFinal_ex(impl->context, reinterpret_cast<unsigned char*>(digest.data()), &size));
    if (size != digest.size())
        throw TransferError(TransferErrorCode::io, "unexpected SHA-256 size");
    return digest;
}
Source::Source(const std::filesystem::path& path) {
    descriptor = ::open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    if (descriptor < 0)
        file_error("open input file");
    struct stat status{};
    if (fstat(descriptor, &status) != 0 || !S_ISREG(status.st_mode) || status.st_size < 0) {
        ::close(descriptor);
        descriptor = -1;
        throw TransferError(TransferErrorCode::io,
                            "input must be a regular file, not a symlink or directory");
    }
    size = static_cast<std::uint64_t>(status.st_size);
}
Source::~Source() {
    if (descriptor >= 0)
        ::close(descriptor);
}
std::size_t Source::read(std::span<std::byte> bytes) {
    ssize_t count;
    do {
        count = ::read(descriptor, bytes.data(), bytes.size());
    } while (count < 0 && errno == EINTR);
    if (count < 0)
        file_error("read input file");
    return static_cast<std::size_t>(count);
}
void Source::rewind() {
    if (lseek(descriptor, 0, SEEK_SET) < 0)
        file_error("rewind input file");
}
std::uint64_t random_id() {
    std::uint64_t id{};
    do {
        crypto_check(RAND_bytes(reinterpret_cast<unsigned char*>(&id), sizeof(id)));
    } while (id == 0);
    return id;
}
ReceiveDirectory::ReceiveDirectory(const std::filesystem::path& path) {
    descriptor = ::open(path.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (descriptor < 0)
        file_error("open receive directory (must already exist and not be a symlink)");
}
ReceiveDirectory::~ReceiveDirectory() {
    if (descriptor >= 0)
        ::close(descriptor);
}
Destination::Destination(const std::filesystem::path& output, const std::string& name)
    : Destination(ReceiveDirectory{output}, name) {}
Destination::Destination(const ReceiveDirectory& output, const std::string& name) : filename(name) {
    if (!is_safe_filename(name))
        throw TransferError(TransferErrorCode::io, "unsafe receive filename");
    directory = fcntl(output.descriptor, F_DUPFD_CLOEXEC, 0);
    if (directory < 0)
        file_error("retain receive directory");
    try {
        struct stat status{};
        if (fstatat(directory, filename.c_str(), &status, AT_SYMLINK_NOFOLLOW) == 0)
            throw TransferError(TransferErrorCode::destination_exists,
                                "destination already exists; refusing to overwrite");
        if (errno != ENOENT)
            file_error("check destination");
        temporary = ".beam-" + std::to_string(random_id()) + ".part";
        descriptor = openat(directory, temporary.c_str(),
                            O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
        if (descriptor < 0) {
            temporary.clear();
            file_error("create receive file");
        }
    } catch (...) {
        ::close(directory);
        directory = -1;
        throw;
    }
}
Destination::~Destination() {
    if (descriptor >= 0)
        ::close(descriptor);
    if (directory >= 0) {
        if (!temporary.empty())
            unlinkat(directory, temporary.c_str(), 0);
        ::close(directory);
    }
}
void Destination::write(std::span<const std::byte> bytes) {
    while (!bytes.empty()) {
        const auto count = ::write(descriptor, bytes.data(), bytes.size());
        if (count < 0 && errno == EINTR)
            continue;
        if (count <= 0)
            file_error("write receive file");
        bytes = bytes.subspan(static_cast<std::size_t>(count));
    }
}
void Destination::commit() {
    if (fsync(descriptor) != 0)
        file_error("flush receive file");
    // Both names are relative to the held directory handle. linkat never replaces
    // an existing file or symlink; the final name appears only after verification.
    if (linkat(directory, temporary.c_str(), directory, filename.c_str(), 0) != 0)
        file_error("publish verified file without overwriting");
    if (unlinkat(directory, temporary.c_str(), 0) != 0)
        file_error("remove receive temporary file");
    temporary.clear();
    if (fsync(directory) != 0)
        file_error("flush receive directory");
}
} // namespace beam::transfer
