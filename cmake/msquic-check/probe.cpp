#include <msquic.h>

#include <cstdio>

int main() {
    [[maybe_unused]] const auto flags = QUIC_CREDENTIAL_FLAG_SET_CA_CERTIFICATE_FILE |
                                        QUIC_CREDENTIAL_FLAG_INDICATE_CERTIFICATE_RECEIVED |
                                        QUIC_CREDENTIAL_FLAG_USE_PORTABLE_CERTIFICATES |
                                        QUIC_CREDENTIAL_FLAG_USE_TLS_BUILTIN_CERTIFICATE_VALIDATION;
    const QUIC_API_TABLE* api = nullptr;
    auto status = MsQuicOpen2(&api);
    if (QUIC_FAILED(status)) {
        std::printf("MsQuicOpen2 failed (%d)\n", static_cast<int>(status));
        return 1;
    }
    uint32_t version[4]{};
    uint32_t size = sizeof(version);
    status = api->GetParam(nullptr, QUIC_PARAM_GLOBAL_LIBRARY_VERSION, &size, version);
    MsQuicClose(api);
    if (QUIC_FAILED(status) || size != sizeof(version)) {
        std::printf("MsQuic version query failed (%d)\n", static_cast<int>(status));
        return 2;
    }
    std::printf("%u.%u.%u\n", version[0], version[1], version[2]);
    const bool supported =
        version[0] == 2 && (version[1] > 6 || (version[1] == 6 && version[2] >= 2));
    return supported ? 0 : 3;
}
