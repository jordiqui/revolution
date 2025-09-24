#include "experience_io.h"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <string>

#ifdef _WIN32
#    include <windows.h>
#else
#    include <sys/stat.h>
#    include <unistd.h>
#endif

namespace {

#ifdef _WIN32
bool fileExists(const std::string& path) {
    DWORD attr = GetFileAttributesA(path.c_str());
    return (attr != INVALID_FILE_ATTRIBUTES) && !(attr & FILE_ATTRIBUTE_DIRECTORY);
}

std::int64_t fileSize(const std::string& path) {
    WIN32_FILE_ATTRIBUTE_DATA data;
    if (!GetFileAttributesExA(path.c_str(), GetFileExInfoStandard, &data))
        return -1;
    LARGE_INTEGER size{};
    size.HighPart = static_cast<LONG>(data.nFileSizeHigh);
    size.LowPart  = data.nFileSizeLow;
    return static_cast<std::int64_t>(size.QuadPart);
}
#else
bool fileExists(const std::string& path) {
    struct stat st {
    };
    return ::stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

std::int64_t fileSize(const std::string& path) {
    struct stat st {
    };
    if (::stat(path.c_str(), &st) != 0)
        return -1;
    return st.st_size;
}
#endif

void log_err(const char* msg, const std::string& path) {
    std::fprintf(stderr, "[Experience] %s: %s", msg, path.c_str());
    if (errno) {
        std::fprintf(stderr, " (errno=%d: %s)", errno, std::strerror(errno));
    }
    std::fprintf(stderr, "\n");
}

bool ensureWritable(const std::string& path, const void* data, std::size_t len, bool allowTruncate) {
    std::FILE* f = std::fopen(path.c_str(), "rb+");
    if (!f) {
        errno = 0;
        f     = std::fopen(path.c_str(), allowTruncate ? "wb+" : "rb+");
        if (!f) {
            log_err("open failed", path);
            return false;
        }
        if (allowTruncate && data && len) {
            if (std::fwrite(data, len, 1, f) != 1) {
                log_err("write header failed", path);
                std::fclose(f);
                return false;
            }
            std::fflush(f);
        }
    }
    std::fclose(f);
    return true;
}

}  // namespace

bool Experience_FileLooksLikeSugar(const ExpHeader& h) {
    const char* raw = reinterpret_cast<const char*>(&h);
    const char* sugarSig = "SugaR";
    return std::memcmp(raw, sugarSig, std::strlen(sugarSig)) == 0;
}

bool Experience_ReadHeader(const std::string& path, ExpHeader& out) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) {
        log_err("open rb failed", path);
        return false;
    }
    if (std::fread(&out, sizeof(out), 1, f) != 1) {
        std::fclose(f);
        log_err("short header", path);
        return false;
    }
    std::fclose(f);
    return true;
}

bool Experience_WriteHeader(const std::string& path, const ExpHeader& h) {
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) {
        log_err("open wb failed", path);
        return false;
    }
    if (std::fwrite(&h, sizeof(h), 1, f) != 1) {
        std::fclose(f);
        log_err("write header failed", path);
        return false;
    }
    std::fclose(f);
    return true;
}

bool Experience_IsCompatible(const ExpHeader& h) {
    if (h.magic != kMagic_Accepted1)
        return false;
    if (h.record_size != kEngineRecordSize)
        return false;
    return true;
}

bool Experience_InitNew(const std::string& path) {
    ExpHeader h{};
    h.magic       = kMagic_Accepted1;
    h.ver_flags   = kDefaultVerFlags;
    h.field3      = kDefaultField3;
    h.field4      = kDefaultField4;
    h.record_size = kEngineRecordSize;
    h.marker      = kDefaultMarker;
    return Experience_WriteHeader(path, h);
}

bool Experience_OpenForReadWrite(const std::string& path) {
    const bool exists = fileExists(path);
    if (exists) {
        ExpHeader header{};
        if (!Experience_ReadHeader(path, header)) {
            std::int64_t size = fileSize(path);
            if (size > 0 && size <= static_cast<std::int64_t>(sizeof(ExpHeader)))
                return Experience_InitNew(path);
            return false;
        }

        const bool sugarLegacy = Experience_FileLooksLikeSugar(header);
        if (!Experience_IsCompatible(header)) {
            if (sugarLegacy) {
                // Legacy SugaR file: don't truncate, just ensure we can write
                return ensureWritable(path, nullptr, 0, false);
            }
#ifdef _WIN32
            std::int64_t size = -1;
#else
            std::int64_t size = fileSize(path);
#endif
            if (size > static_cast<std::int64_t>(sizeof(ExpHeader))) {
                log_err("incompatible header detected; refusing to truncate non-header file", path);
                return false;
            }
            log_err("incompatible header; re-initializing", path);
            return Experience_InitNew(path);
        }

        return ensureWritable(path, &header, sizeof(header), true);
    }

    return Experience_InitNew(path);
}

