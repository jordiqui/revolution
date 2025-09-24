#include "experience_io.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>
#include <iostream>

#include "misc.h"

#if defined(_MSC_VER)
#    define PACKED_STRUCT_BEGIN __pragma(pack(push, 1))
#    define PACKED_STRUCT_END __pragma(pack(pop))
#    define PACKED_STRUCT struct
#else
#    define PACKED_STRUCT_BEGIN
#    define PACKED_STRUCT_END
#    define PACKED_STRUCT struct __attribute__((packed))
#endif

#ifdef _WIN32
#    include <io.h>
#    include <windows.h>
#    include <cwchar>
#else
#    include <fcntl.h>
#    include <sys/stat.h>
#    include <sys/types.h>
#    include <unistd.h>
#endif

namespace {

class ExperienceWriteLock {
   public:
    explicit ExperienceWriteLock(const std::filesystem::path& target) {
#ifdef _WIN32
        const std::string targetUtf8 = target.u8string();
        std::uint64_t     hash       = 1469598103934665603ULL; // FNV-1a 64-bit offset basis
        for (unsigned char c : targetUtf8) {
            hash ^= static_cast<std::uint64_t>(c);
            hash *= 1099511628211ULL; // FNV-1a 64-bit prime
        }

        wchar_t hashBuffer[32];
        std::swprintf(hashBuffer, sizeof(hashBuffer) / sizeof(hashBuffer[0]), L"%016llX",
                      static_cast<unsigned long long>(hash));

        auto try_mutex = [](const std::wstring& name) -> HANDLE {
            if (name.empty())
                return nullptr;
            return CreateMutexW(nullptr, FALSE, name.c_str());
        };

        const std::wstring suffix = L"RevolutionExperience-" + std::wstring(hashBuffer);

        std::wstring candidates[] = {L"Global\\" + suffix, L"Local\\" + suffix, suffix};

        for (const auto& candidate : candidates) {
            handle = try_mutex(candidate);
            if (handle)
                break;
        }

        if (handle)
            locked = WaitForSingleObject(handle, INFINITE) == WAIT_OBJECT_0;
#else
        std::filesystem::path lockPath = target;
        lockPath += ".lock";
        fd = ::open(lockPath.c_str(), O_CREAT | O_RDWR, 0666);
        if (fd >= 0) {
            struct flock fl {
            };
            fl.l_type   = F_WRLCK;
            fl.l_whence = SEEK_SET;
            fl.l_start  = 0;
            fl.l_len    = 0;
            locked      = fcntl(fd, F_SETLKW, &fl) != -1;
            if (!locked) {
                ::close(fd);
                fd = -1;
            }
        }
#endif
    }

    ExperienceWriteLock(const ExperienceWriteLock&)            = delete;
    ExperienceWriteLock& operator=(const ExperienceWriteLock&) = delete;

    ~ExperienceWriteLock() {
#ifdef _WIN32
        if (handle) {
            if (locked)
                ReleaseMutex(handle);
            CloseHandle(handle);
        }
#else
        if (fd >= 0) {
            struct flock fl {
            };
            fl.l_type   = F_UNLCK;
            fl.l_whence = SEEK_SET;
            fl.l_start  = 0;
            fl.l_len    = 0;
            fcntl(fd, F_SETLK, &fl);
            ::close(fd);
        }
#endif
    }

    bool owns_lock() const { return locked; }

   private:
#ifdef _WIN32
    HANDLE handle = nullptr;
#else
    int  fd     = -1;
#endif
    bool locked = false;
};

bool is_pow2(std::uint32_t value) {
    return value && ((value & (value - 1)) == 0);
}

std::uint32_t crc32(const void* data, std::size_t length) {
    static std::uint32_t table[256];
    static bool          tableInit = false;

    if (!tableInit) {
        for (std::uint32_t i = 0; i < 256; ++i) {
            std::uint32_t c = i;
            for (int j = 0; j < 8; ++j)
                c = (c >> 1) ^ (0xEDB88320U & (-(c & 1u)));
            table[i] = c;
        }
        tableInit = true;
    }

    const auto* bytes = static_cast<const std::uint8_t*>(data);
    std::uint32_t crc = 0xFFFFFFFFU;
    for (std::size_t i = 0; i < length; ++i)
        crc = table[(crc ^ bytes[i]) & 0xFFu] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFU;
}

std::filesystem::path normalize_path(const std::string& raw) {
    std::filesystem::path path(raw);
    path = path.lexically_normal();
    if (path.is_relative()) {
        std::error_code ec;
        auto absolutePath = std::filesystem::absolute(path, ec);
        if (!ec)
            path = absolutePath;
    }
    return path;
}

std::string to_display_string(const std::filesystem::path& path) {
#ifdef _WIN32
    return path.u8string();
#else
    return path.string();
#endif
}

void log_info(const std::string& message) {
    Stockfish::sync_cout_start();
    std::cout << "info string " << message << std::endl;
    Stockfish::sync_cout_end();
}

void log_error(const std::string& message, const std::filesystem::path& path) {
    const int savedErrno = errno;
    std::string text     = message + ": '" + to_display_string(path) + "'";
    if (savedErrno)
        text += " (errno=" + std::to_string(savedErrno) + ": " + std::strerror(savedErrno) + ")";
    log_info("experience: " + text);
}

#ifdef _WIN32
void clear_readonly_attribute(const std::filesystem::path& path) {
    std::wstring wide = path.wstring();
    DWORD        attr = GetFileAttributesW(wide.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES)
        return;
    if (attr & FILE_ATTRIBUTE_READONLY)
        SetFileAttributesW(wide.c_str(), attr & ~FILE_ATTRIBUTE_READONLY);
}
#else
void clear_readonly_attribute(const std::filesystem::path&) {}
#endif

std::size_t expected_file_size(std::uint32_t buckets) {
    return sizeof(ExperienceHeader) + std::size_t(buckets) * sizeof(ExperienceRecord);
}

std::string lowercase_extension(const std::filesystem::path& path) {
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return ext;
}

PACKED_STRUCT_BEGIN
PACKED_STRUCT PACKED_SugaRHeader {
    std::uint8_t  version;
    std::uint64_t seed;
    std::uint32_t bucketSize;
    std::uint32_t entrySize;
};

PACKED_STRUCT PACKED_SugaRMetaBlock {
    std::uint32_t hashBits;
    std::uint32_t reserved;
    std::uint16_t endianTag;
    float         kFactor;
    std::uint64_t counters;
};

PACKED_STRUCT PACKED_SugaRBinV2 {
    std::uint64_t key;
    std::uint16_t move;
    std::int16_t  score;
    std::int16_t  depth;
    std::int16_t  count;
    std::int32_t  wins;
    std::int32_t  losses;
    std::int32_t  draws;
    std::int16_t  flags;
    std::int16_t  age;
    std::int16_t  pad;
};
PACKED_STRUCT_END

static_assert(sizeof(PACKED_SugaRHeader) == 1 + sizeof(std::uint64_t) + 2 * sizeof(std::uint32_t),
              "Unexpected SugaR header packing");
static_assert(sizeof(PACKED_SugaRMetaBlock)
                == sizeof(std::uint32_t) * 2 + sizeof(std::uint16_t) + sizeof(float)
                     + sizeof(std::uint64_t),
              "Unexpected SugaR meta block packing");
static_assert(sizeof(PACKED_SugaRBinV2) == 34, "Unexpected SugaR entry size");

bool write_empty_sugar_experience(const std::filesystem::path& target) {
    ensure_parent_directory(target);
    clear_readonly_attribute(target);

    ExperienceWriteLock guard(target);
    if (!guard.owns_lock()) {
        log_info("experience: failed to acquire write lock for '" + to_display_string(target) + "'");
        return false;
    }

    std::ofstream out(target, std::ios::binary | std::ios::trunc);
    if (!out) {
        log_error("failed to open for writing", target);
        return false;
    }

    static constexpr char kSignature[] = "SugaR Experience version 2";
    out.write(kSignature, sizeof(kSignature) - 1);

    const PACKED_SugaRHeader header{2u, 0x103380A463E28000ULL, 6u,
                                    static_cast<std::uint32_t>(sizeof(PACKED_SugaRBinV2))};
    const PACKED_SugaRMetaBlock metaBlocks[2] = {{23u, 1u, 0x0002u, 11.978f, 0u},
                                                 {23u, 1u, 0x0002u, 11.978f, 0u}};

    out.write(reinterpret_cast<const char*>(&header), sizeof(header));
    out.write(reinterpret_cast<const char*>(metaBlocks), sizeof(metaBlocks));

    if (!out) {
        log_error("failed to write header", target);
        return false;
    }

    out.flush();
    return true;
}

std::FILE* fopen_compat(const std::filesystem::path& path, const char* mode) {
#ifdef _WIN32
    std::wstring wideMode;
    for (const char* p = mode; *p; ++p)
        wideMode.push_back(static_cast<wchar_t>(*p));
    return _wfopen(path.c_str(), wideMode.c_str());
#else
    return std::fopen(path.c_str(), mode);
#endif
}

void ensure_parent_directory(const std::filesystem::path& path) {
    auto parent = path.parent_path();
    if (parent.empty() || parent == ".")
        return;
    std::error_code ec;
    std::filesystem::create_directories(parent, ec);
}

ExperienceHeader make_header(std::uint32_t buckets) {
    ExperienceHeader header{};
    header.magic      = kExperienceMagic;
    header.version    = kExperienceVersion;
    header.recordSize = static_cast<std::uint32_t>(sizeof(ExperienceRecord));
    header.bucketCount = buckets;
    header.headerSize  = static_cast<std::uint32_t>(sizeof(ExperienceHeader));
    header.headerCrc32 = 0;
    header.headerCrc32 = crc32(&header, sizeof(header));
    return header;
}

bool validate_header(const ExperienceHeader& header, std::string* reason = nullptr) {
    if (header.magic != kExperienceMagic) {
        if (reason)
            *reason = "magic mismatch";
        return false;
    }
    if (header.version != kExperienceVersion) {
        if (reason)
            *reason = "version mismatch";
        return false;
    }
    if (header.headerSize != sizeof(ExperienceHeader)) {
        if (reason)
            *reason = "header size mismatch";
        return false;
    }
    if (header.recordSize != sizeof(ExperienceRecord)) {
        if (reason)
            *reason = "record size mismatch";
        return false;
    }
    if (!is_pow2(header.bucketCount)) {
        if (reason)
            *reason = "bucket count not power of two";
        return false;
    }

    ExperienceHeader temp = header;
    temp.headerCrc32      = 0;
    if (crc32(&temp, sizeof(temp)) != header.headerCrc32) {
        if (reason)
            *reason = "header CRC mismatch";
        return false;
    }

    return true;
}

bool read_header_from_stream(std::istream& in, ExperienceHeader& header) {
    in.read(reinterpret_cast<char*>(&header), sizeof(header));
    return bool(in);
}

bool write_zero_filled(const std::filesystem::path& target, std::uint32_t buckets, ExperienceHeader& outHeader) {
    ensure_parent_directory(target);

    ExperienceWriteLock guard(target);
    if (!guard.owns_lock()) {
        log_info("experience: failed to acquire write lock for '" + to_display_string(target)
                 + "'");
        return false;
    }

    const ExperienceHeader header = make_header(buckets);
    const auto             total  = expected_file_size(buckets);

    std::filesystem::path tmp = target;
    tmp += ".tmp";

    clear_readonly_attribute(target);

    errno = 0;
    std::FILE* f = fopen_compat(tmp, "wb");
    if (!f) {
        log_error("failed to open temp file for writing", tmp);
        return false;
    }

    auto cleanup = [&]() {
        std::fclose(f);
        std::error_code removeEc;
        std::filesystem::remove(tmp, removeEc);
    };

    if (std::fwrite(&header, sizeof(header), 1, f) != 1) {
        log_error("failed to write header", tmp);
        cleanup();
        return false;
    }

    const std::size_t payload = total - sizeof(header);
    std::vector<std::uint8_t> zeros(std::min<std::size_t>(payload, 1u << 16), 0);
    std::size_t               remaining = payload;
    while (remaining) {
        const std::size_t chunk = std::min<std::size_t>(zeros.size(), remaining);
        if (chunk && std::fwrite(zeros.data(), 1, chunk, f) != chunk) {
            log_error("failed while zero-filling", tmp);
            cleanup();
            return false;
        }
        remaining -= chunk;
    }

    std::fflush(f);
#ifdef _WIN32
    _commit(_fileno(f));
#else
    ::fsync(fileno(f));
#endif
    std::fclose(f);

    std::error_code ec;
    std::filesystem::rename(tmp, target, ec);
    if (ec) {
        std::filesystem::remove(target, ec);
        std::filesystem::rename(tmp, target, ec);
    }
    if (ec) {
        std::filesystem::remove(tmp, ec);
        log_error("atomic rename failed", target);
        return false;
    }

    outHeader = header;
    return true;
}

bool ensure_readonly_status(const std::filesystem::path& path, bool& readOnly) {
    errno = 0;
    std::FILE* f = fopen_compat(path, "rb+");
    if (f) {
        std::fclose(f);
        readOnly = false;
        return true;
    }

    errno = 0;
    f = fopen_compat(path, "rb");
    if (!f)
        return false;
    std::fclose(f);
    readOnly = true;
    log_info("experience: path not writable, continuing read-only");
    return true;
}

}  // namespace

bool Experience_WriteBufferAtomically(const std::string& path, const std::string& buffer) {
    if (path.empty())
        return false;

    const std::filesystem::path target = normalize_path(path);
    ensure_parent_directory(target);
    ExperienceWriteLock         guard(target);
    if (!guard.owns_lock()) {
        log_info("experience: failed to acquire write lock for '" + to_display_string(target) + "'");
        return false;
    }

    std::filesystem::path tmp = target;
    tmp += ".tmp";

    clear_readonly_attribute(target);

    errno = 0;
    std::FILE* f = fopen_compat(tmp, "wb");
    if (!f) {
        log_error("failed to open temp file for writing", tmp);
        return false;
    }

    auto cleanup = [&]() {
        std::fclose(f);
        std::error_code removeEc;
        std::filesystem::remove(tmp, removeEc);
    };

    if (!buffer.empty() && std::fwrite(buffer.data(), 1, buffer.size(), f) != buffer.size()) {
        log_error("failed while writing buffer", tmp);
        cleanup();
        return false;
    }

    std::fflush(f);
#ifdef _WIN32
    _commit(_fileno(f));
#else
    ::fsync(fileno(f));
#endif
    std::fclose(f);

    std::error_code ec;
    std::filesystem::rename(tmp, target, ec);
    if (ec) {
        std::filesystem::remove(target, ec);
        std::filesystem::rename(tmp, target, ec);
    }
    if (ec) {
        std::filesystem::remove(tmp, ec);
        log_error("atomic rename failed", target);
        return false;
    }

    return true;
}

std::uint32_t Experience_NormalizeBucketCount(std::uint32_t requested) {
    if (requested < kMinExperienceBuckets)
        requested = kMinExperienceBuckets;
    if (requested > kMaxExperienceBuckets)
        requested = kMaxExperienceBuckets;

    if (!is_pow2(requested)) {
        std::uint32_t pow2 = kMinExperienceBuckets;
        while (pow2 < requested && pow2 < kMaxExperienceBuckets)
            pow2 <<= 1;
        if (!is_pow2(pow2))
            pow2 = kDefaultExperienceBuckets;
        requested = pow2;
    }
    return requested;
}

bool Experience_FileLooksLikeSugar(const ExperienceHeader& header) {
    const char* raw     = reinterpret_cast<const char*>(&header);
    const char* sugar   = "SugaR";
    const size_t length = std::strlen(sugar);
    return std::memcmp(raw, sugar, length) == 0;
}

bool Experience_IsCompatible(const ExperienceHeader& header) {
    return validate_header(header);
}

bool Experience_ReadHeader(const std::string& path, ExperienceHeader& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        log_error("failed to open for reading", std::filesystem::path(path));
        return false;
    }
    if (!read_header_from_stream(in, out)) {
        log_error("short header", std::filesystem::path(path));
        return false;
    }
    return true;
}

bool Experience_WriteHeader(const std::string& path, const ExperienceHeader& header) {
    std::ofstream out(path, std::ios::binary | std::ios::in | std::ios::out);
    if (!out) {
        out.open(path, std::ios::binary | std::ios::trunc);
        if (!out) {
            log_error("failed to open for header write", std::filesystem::path(path));
            return false;
        }
    }

    out.seekp(0, std::ios::beg);
    out.write(reinterpret_cast<const char*>(&header), sizeof(header));
    if (!out) {
        log_error("failed to write header", std::filesystem::path(path));
        return false;
    }
    out.flush();
    return true;
}

bool Experience_InitNew(const std::string& path, std::uint32_t bucketCount) {
    const std::filesystem::path normalized = normalize_path(path);

    const std::string ext = lowercase_extension(normalized);
    if (ext == ".exp") {
        if (!write_empty_sugar_experience(normalized))
            return false;

        log_info("experience: created '" + to_display_string(normalized)
                 + "' in SugaR v2 format (empty header)");
        return true;
    }

    const std::uint32_t normalizedBuckets = Experience_NormalizeBucketCount(bucketCount);
    if (normalizedBuckets != bucketCount)
        log_info("experience: bucket count " + std::to_string(bucketCount)
                 + " normalized to " + std::to_string(normalizedBuckets));

    ExperienceHeader header{};
    if (!write_zero_filled(normalized, normalizedBuckets, header))
        return false;

    log_info("experience: created '" + to_display_string(normalized) + "' with "
             + std::to_string(normalizedBuckets) + " buckets (record="
             + std::to_string(sizeof(ExperienceRecord)) + ", version="
             + std::to_string(kExperienceVersion) + ")");
    return true;
}

ExperienceOpenResult Experience_OpenForReadWrite(const std::string& path, std::uint32_t requestedBuckets) {
    ExperienceOpenResult result{};
    result.normalizedPath = path;

    if (path.empty())
        return result;

    const std::filesystem::path normalized = normalize_path(path);
    result.normalizedPath                  = to_display_string(normalized);

    ensure_parent_directory(normalized);
    std::error_code ec;

    const std::string ext = lowercase_extension(normalized);
    if (ext == ".exp") {
        if (!std::filesystem::exists(normalized)) {
            if (!Experience_InitNew(normalized.string(), requestedBuckets))
                return result;
            result.recreated = true;
        }

        std::ifstream in(normalized, std::ios::binary);
        if (!in) {
            log_error("failed to open for reading", normalized);
            return result;
        }

        std::fstream probe(normalized, std::ios::binary | std::ios::in | std::ios::out);
        if (!probe) {
            result.readOnly = true;
            log_info("experience: path not writable, continuing read-only");
        }

        result.ok          = true;
        result.bucketCount = 0;
        result.recordSize  = 0;
        result.version     = 0;
        return result;
    }

    const std::uint32_t normalizedBuckets = Experience_NormalizeBucketCount(requestedBuckets);
    if (normalizedBuckets != requestedBuckets)
        log_info("experience: bucket count " + std::to_string(requestedBuckets)
                 + " normalized to " + std::to_string(normalizedBuckets));

    ExperienceHeader header{};
    bool             exists = std::filesystem::exists(normalized);

    if (!exists) {
        if (!Experience_InitNew(normalized.string(), normalizedBuckets))
            return result;
        if (!Experience_ReadHeader(normalized.string(), header))
            return result;
        result.recreated = true;
    } else {
        if (!Experience_ReadHeader(normalized.string(), header))
            header.headerSize = 0;  // force recreation below

        std::string reason;
        bool        valid = false;
        if (header.headerSize == sizeof(ExperienceHeader))
            valid = validate_header(header, &reason);
        else
            reason = "header size mismatch";

        if (valid) {
            std::uintmax_t size = std::filesystem::file_size(normalized, ec);
            const auto     need = expected_file_size(header.bucketCount);
            if (ec || size != need) {
                valid  = false;
                reason = "unexpected file size";
            }
        }

        if (!valid) {
            const std::string details = reason.empty() ? std::string{}
                                                       : " (" + reason + ")";
            log_info("experience: invalid or truncated file '" + to_display_string(normalized)
                     + details + "' -> recreated with "
                     + std::to_string(normalizedBuckets) + " buckets (record="
                     + std::to_string(sizeof(ExperienceRecord)) + ", version="
                     + std::to_string(kExperienceVersion) + ")");
            if (!write_zero_filled(normalized, normalizedBuckets, header))
                return result;
            result.recreated = true;
        }
    }

    if (!ensure_readonly_status(normalized, result.readOnly))
        return result;

    result.ok          = true;
    result.bucketCount = header.bucketCount;
    result.recordSize  = header.recordSize;
    result.version     = header.version;
    return result;
}

#undef PACKED_STRUCT
#undef PACKED_STRUCT_BEGIN
#undef PACKED_STRUCT_END

