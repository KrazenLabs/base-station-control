#include "framekit/files.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <system_error>

#if defined(__linux__)
#include <cerrno>
#include <cstring>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace framekit {

namespace {

#if defined(__linux__)
bool writeContent(const std::filesystem::path& path, const std::string& content, const WriteOptions& options,
                  std::string* reason) {
    const mode_t mode = options.ownerOnly ? 0600 : 0644;
    const int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, mode);
    if (fd < 0) {
        *reason = std::strerror(errno);
        return false;
    }
    bool ok = ::fchmod(fd, mode) == 0;
    if (!ok) {
        *reason = std::strerror(errno);
    }
    size_t written = 0;
    while (ok && written < content.size()) {
        const ssize_t result = ::write(fd, content.data() + written, content.size() - written);
        if (result < 0 && errno == EINTR) {
            continue;
        }
        if (result <= 0) {
            *reason = result < 0 ? std::strerror(errno) : "nothing was written";
            ok = false;
            break;
        }
        written += static_cast<size_t>(result);
    }
    if (ok && options.durable && ::fsync(fd) != 0) {
        *reason = std::strerror(errno);
        ok = false;
    }
    if (::close(fd) != 0 && ok) {
        *reason = std::strerror(errno);
        ok = false;
    }
    return ok;
}

void syncDirectory(const std::filesystem::path& directory) {
    const int fd = ::open(directory.empty() ? "." : directory.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (fd >= 0) {
        ::fsync(fd);
        ::close(fd);
    }
}
#else
bool writeContent(const std::filesystem::path& path, const std::string& content, const WriteOptions& options,
                  std::string* reason) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        *reason = "cannot be opened for writing";
        return false;
    }
    file << content;
    file.flush();
    if (options.ownerOnly) {
        std::error_code ec;
        std::filesystem::permissions(path, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
                                     std::filesystem::perm_options::replace, ec);
    }
    if (!file) {
        *reason = "writing failed";
        return false;
    }
    return true;
}

void syncDirectory(const std::filesystem::path&) {}
#endif

}

#if defined(__linux__)
FileRead readFileChecked(const std::filesystem::path& path) {
    FileRead result;
    const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NONBLOCK);
    if (fd < 0) {
        const int code = errno;
        result.status = code == ENOENT || code == ENOTDIR ? ReadStatus::Missing : ReadStatus::Unreadable;
        result.error = std::strerror(code);
        return result;
    }
    char chunk[16384];
    while (true) {
        const ssize_t count = ::read(fd, chunk, sizeof(chunk));
        if (count > 0) {
            result.content.append(chunk, static_cast<size_t>(count));
            continue;
        }
        if (count < 0 && errno == EINTR) {
            continue;
        }
        const int code = errno;
        ::close(fd);
        if (count < 0) {
            result.status = ReadStatus::Unreadable;
            result.error = std::strerror(code);
            result.content.clear();
            return result;
        }
        result.status = ReadStatus::Ok;
        return result;
    }
}
#else
FileRead readFileChecked(const std::filesystem::path& path) {
    FileRead result;
    std::error_code ec;
    const std::filesystem::file_status status = std::filesystem::status(path, ec);
    if (status.type() == std::filesystem::file_type::not_found) {
        result.status = ReadStatus::Missing;
        result.error = "No such file or directory";
        return result;
    }
    result.status = ReadStatus::Unreadable;
    if (ec) {
        result.error = ec.message();
        return result;
    }
    if (status.type() == std::filesystem::file_type::directory) {
        result.error = "Is a directory";
        return result;
    }
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        result.error = "cannot be opened";
        return result;
    }
    std::ostringstream contents;
    contents << file.rdbuf();
    result.status = ReadStatus::Ok;
    result.error.clear();
    result.content = contents.str();
    return result;
}
#endif

std::optional<std::string> readFile(const std::filesystem::path& path) {
    FileRead read = readFileChecked(path);
    if (read.status != ReadStatus::Ok) {
        return std::nullopt;
    }
    return std::move(read.content);
}

bool writeFileAtomically(const std::filesystem::path& path, const std::string& content, std::string* error,
                         const WriteOptions& options) {
    std::error_code ec;
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path(), ec);
    }
    std::filesystem::path temporary = path;
    temporary += ".tmp";
    std::string reason;
    if (!writeContent(temporary, content, options, &reason)) {
        if (error) {
            *error = "cannot write " + temporary.string() + ": " + reason;
        }
        std::filesystem::remove(temporary, ec);
        return false;
    }
    std::filesystem::rename(temporary, path, ec);
    if (ec) {
        if (error) {
            *error = "cannot replace " + path.string() + ": " + ec.message();
        }
        std::filesystem::remove(temporary, ec);
        return false;
    }
    if (options.durable) {
        syncDirectory(path.parent_path());
    }
    return true;
}

std::vector<std::string> listDirectory(const std::filesystem::path& directory) {
    std::vector<std::string> names;
    std::error_code ec;
    std::filesystem::directory_iterator iterator(directory, ec);
    if (ec) {
        return names;
    }
    for (const std::filesystem::directory_entry& entry : iterator) {
        names.push_back(entry.path().filename().string());
    }
    std::sort(names.begin(), names.end());
    return names;
}

}
