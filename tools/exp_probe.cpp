#include <cstdio>
#include <cstdint>
#include <stdexcept>
#include <string>

#pragma pack(push, 1)
struct ExpHeader {
    std::uint32_t magic;
    std::uint32_t ver_flags;
    std::uint32_t field3;
    std::uint32_t field4;
    std::uint32_t record_size;
    std::uint32_t marker;
};
#pragma pack(pop)
static_assert(sizeof(ExpHeader) == 24, "header must be 24 bytes");

static ExpHeader readHeader(const char* path) {
    std::FILE* f = std::fopen(path, "rb");
    if (!f)
        throw std::runtime_error(std::string("open fail: ") + path);
    ExpHeader h{};
    if (std::fread(&h, sizeof(h), 1, f) != 1) {
        std::fclose(f);
        throw std::runtime_error("short read");
    }
    std::fclose(f);
    return h;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s file1.exp [file2.exp...]\n", argv[0]);
        return 1;
    }
    for (int i = 1; i < argc; ++i) {
        try {
            auto h = readHeader(argv[i]);
            std::printf("File: %s\n", argv[i]);
            std::printf("  magic       = 0x%08X\n", h.magic);
            std::printf("  ver_flags   = 0x%08X\n", h.ver_flags);
            std::printf("  field3      = %u (0x%08X)\n", h.field3, h.field3);
            std::printf("  field4      = %u (0x%08X)\n", h.field4, h.field4);
            std::printf("  record_size = %u (0x%08X)\n", h.record_size, h.record_size);
            std::printf("  marker      = %u (0x%08X)\n", h.marker, h.marker);
        } catch (const std::exception& e) {
            std::fprintf(stderr, "[ERR] %s: %s\n", argv[i], e.what());
        }
    }
    return 0;
}

