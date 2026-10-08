#include "beam/transfer.hpp"

namespace beam {
const char* error_name(TransferErrorCode code) noexcept {
    switch (code) {
    case TransferErrorCode::none:
        return "none";
    case TransferErrorCode::cancelled:
        return "cancelled";
    case TransferErrorCode::rejected:
        return "rejected";
    case TransferErrorCode::destination_exists:
        return "destination exists";
    case TransferErrorCode::io:
        return "file I/O failed";
    case TransferErrorCode::integrity:
        return "integrity verification failed";
    case TransferErrorCode::size:
        return "payload size mismatch";
    case TransferErrorCode::protocol:
        return "invalid protocol";
    case TransferErrorCode::timeout:
        return "timed out";
    case TransferErrorCode::transport:
        return "connection failed";
    case TransferErrorCode::source_changed:
        return "input file changed";
    }
    return "unknown error";
}
} // namespace beam
