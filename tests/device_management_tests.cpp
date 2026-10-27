#include "beam/identity.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <sys/stat.h>
#ifdef __APPLE__
#include <unistd.h>
#else
#include <stdlib.h>
#endif

namespace {
void require(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class F> void rejects(F operation) {
    bool failed = false;
    try {
        operation();
    } catch (const std::exception&) {
        failed = true;
    }
    require(failed, "unsafe device operation accepted");
}
struct Workspace {
    std::filesystem::path path;
    Workspace() {
        auto name = (std::filesystem::canonical(std::filesystem::temp_directory_path()) /
                     "beam-devices-XXXXXX")
                        .string();
        require(mkdtemp(name.data()) != nullptr, "create workspace");
        path = name;
    }
    ~Workspace() { std::filesystem::remove_all(path); }
};
void save(const std::filesystem::path& path, const std::string& contents) {
    std::ofstream stream(path);
    stream << contents;
    stream.close();
    require(!stream.fail() && chmod(path.c_str(), 0600) == 0, "save fixture");
}
} // namespace
int main() {
    try {
        Workspace workspace;
        const auto local_path = workspace.path / "local";
        beam::DeviceIdentity bob, carol;
        {
            beam::IdentityStore remote(workspace.path / "bob", true);
            bob = remote.initialize();
        }
        {
            beam::IdentityStore remote(workspace.path / "carol", true);
            carol = remote.initialize();
        }
        save(workspace.path / "bob.pem", bob.certificate_pem);
        save(workspace.path / "carol.pem", carol.certificate_pem);
        std::string backup;
        {
            beam::IdentityStore store(local_path, true);
            const auto local = store.initialize();
            store.pair("phone", workspace.path / "bob.pem", bob.fingerprint, "127.0.0.1:4269");
            // Existing v2 records become generic endpoint pairs when edited.
            save(local_path / "phone.peer",
                 "beam-peer-v2\n127.0.0.1:4269\n100.64.0.2:4269\n" + bob.certificate_pem);
            require(store.peer("phone").fallback_endpoint == "100.64.0.2:4269",
                    "legacy peer lost fallback");
            beam::PeerEdit edit;
            edit.name = "work-phone";
            edit.endpoint = "[::1]:5000";
            edit.fallback_endpoint = "100.64.0.2:5000";
            edit.expected_fingerprint = bob.fingerprint;
            const auto changed = store.edit_peer("phone", edit);
            require(changed.name == "work-phone" &&
                        changed.identity.fingerprint == bob.fingerprint &&
                        store.peers().size() == 1,
                    "rename changed trust or duplicated device");
            rejects([&] { static_cast<void>(store.peer("phone")); });
            edit.name = "../escape";
            rejects([&] { store.edit_peer("work-phone", edit); });
            edit.name = "work-phone";
            edit.endpoint = "host.example:4269";
            rejects([&] { store.edit_peer("work-phone", edit); });
            edit.endpoint = "127.0.0.1:0";
            rejects([&] { store.edit_peer("work-phone", edit); });
            edit.endpoint = "";
            edit.fallback_endpoint = "";
            edit.certificate_pem = carol.certificate_pem;
            edit.verified_fingerprint = bob.fingerprint;
            rejects([&] { store.edit_peer("work-phone", edit); });
            require(store.peer("work-phone").identity.fingerprint == bob.fingerprint,
                    "failed edit changed trust");
            edit.verified_fingerprint = carol.fingerprint;
            store.edit_peer("work-phone", edit);
            require(store.credentials("work-phone").pinned_certificates ==
                        std::vector{carol.certificate_pem},
                    "replacement retained old certificate");
            rejects([&] { store.edit_peer("work-phone", edit); }); // stale expected fingerprint
            store.pair("phone", workspace.path / "bob.pem", bob.fingerprint, "127.0.0.1:4269");
            beam::PeerEdit duplicate;
            duplicate.name = "phone";
            rejects([&] { store.edit_peer("work-phone", duplicate); });
            duplicate = {};
            duplicate.certificate_pem = bob.certificate_pem;
            duplicate.verified_fingerprint = bob.fingerprint;
            rejects([&] { store.edit_peer("work-phone", duplicate); });
            duplicate.certificate_pem = local.certificate_pem;
            duplicate.verified_fingerprint = local.fingerprint;
            rejects([&] { store.edit_peer("work-phone", duplicate); });
            beam::PeerEdit fallback;
            fallback.fallback_endpoint = "192.168.1.10:5000";
            store.edit_peer("phone", fallback);
            backup = store.export_devices();
            auto legacy = backup;
            legacy.replace(0, std::string("beam-devices-backup-v2").size(),
                           "beam-devices-backup-v1");
            require(store.preview_import(legacy).peers[0].fallback_endpoint == "192.168.1.10:5000",
                    "legacy backup lost fallback");
            require(backup.find("PRIVATE KEY") == std::string::npos &&
                        store.preview_import(backup).existing == 2,
                    "bad backup contents");
            require(store.import_devices(backup).added == 0, "restore not idempotent");
            beam::PeerEdit route;
            route.endpoint = "127.0.0.1:6000";
            store.edit_peer("phone", route);
            store.import_devices(backup);
            require(store.peer("phone").endpoint == "127.0.0.1:6000",
                    "import replaced current settings");
            auto corrupt = backup;
            corrupt[corrupt.find(bob.fingerprint)] = 'x';
            rejects([&] { store.import_devices(corrupt); });
            rejects([&] { store.import_devices(backup + "garbage"); });
            rejects([&] { store.import_devices(backup.substr(0, backup.size() - 1)); });
            rejects(
                [&] { store.import_devices(std::string(beam::max_device_backup_size + 1, 'a')); });
            rejects([&] { store.import_devices(backup + '\0'); });
            auto version = backup;
            version.replace(0, 22, "beam-devices-backup-v9\n");
            rejects([&] { store.import_devices(version); });
            store.unpair("phone");
            store.unpair("work-phone");
            require(store.preview_import(backup).added == 2 && store.peers().empty(),
                    "preview changed trust");
            require(store.import_devices(backup).added == 2 && store.peers().size() == 2,
                    "restore failed");
        }
        const auto file = workspace.path / "devices.beam-backup";
        beam::write_device_backup(file, backup);
        require(beam::read_device_backup(file) == backup, "backup file round trip failed");
        rejects([&] { beam::write_device_backup(file, "overwrite"); });
        std::filesystem::create_symlink(file, workspace.path / "link");
        rejects([&] { static_cast<void>(beam::read_device_backup(workspace.path / "link")); });
        // Simulate interruption after the durable commit and one partial write.
        auto legacy_transaction = backup;
        legacy_transaction.replace(0, std::string("beam-devices-backup-v2").size(),
                                   "beam-devices-backup-v1");
        save(local_path / "devices.transaction", legacy_transaction);
        std::filesystem::remove(local_path / "phone.peer");
        {
            beam::IdentityStore recovered(local_path);
            require(recovered.peers().size() == 2 &&
                        recovered.peer("phone").endpoint == "127.0.0.1:4269" &&
                        recovered.peer("phone").fallback_endpoint == "192.168.1.10:5000",
                    "transaction did not recover");
            require(!std::filesystem::exists(local_path / "devices.transaction"),
                    "journal not cleaned");
        }
        {
            beam::IdentityStore other(workspace.path / "other", true);
            other.initialize();
            other.pair("phone", workspace.path / "carol.pem", carol.fingerprint);
            rejects([&] { other.import_devices(backup); });
            require(other.peers().size() == 1, "conflicting import partly applied");
        }
        // Unsafe files block recovery before it writes any record.
        save(local_path / "devices.transaction", backup);
        std::filesystem::remove(local_path / "phone.peer");
        std::filesystem::create_symlink(file, local_path / "phone.peer");
        rejects([&] { beam::IdentityStore unsafe(local_path); });
        require(beam::read_device_backup(file) == backup, "recovery followed symlink");
        std::cout << "device management checks passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
