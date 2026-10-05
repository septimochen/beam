#pragma once

#include <string_view>

namespace beam {

[[nodiscard]] std::string_view version() noexcept;

// Accept one portable ASCII basename. Reject unsafe names instead of rewriting
// them into names that may collide. Filesystem containment is a separate concern.
[[nodiscard]] bool is_safe_filename(std::string_view filename) noexcept;

} // namespace beam
