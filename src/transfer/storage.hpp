#pragma once

#include <fcntl.h>
#include <unistd.h>
#ifdef __linux__
#include <linux/fs.h>
#include <sys/syscall.h>
#endif

namespace beam::storage {
// Publish without replacing any inode, then remove the temporary name.
// Android forbids app-created hard links, so use the Linux no-replace rename.
// Both names stay relative to the caller's held directory handle.
inline int publish_exclusive(int directory, const char* temporary, const char* filename) {
#ifdef __linux__
    return static_cast<int>(
        syscall(SYS_renameat2, directory, temporary, directory, filename, RENAME_NOREPLACE));
#else
    if (linkat(directory, temporary, directory, filename, 0) != 0)
        return -1;
    return unlinkat(directory, temporary, 0);
#endif
}
} // namespace beam::storage
