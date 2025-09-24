#include "experience_io.h"

#include <cstdio>
#include <stdexcept>
#include <string>

static ExperienceHeader readHeader(const char* path) {
    std::FILE* f = std::fopen(path, "rb");
    if (!f)
        throw std::runtime_error(std::string("open fail: ") + path);
    ExperienceHeader h{};
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
            std::printf("  magic       = 0x%016llX\n",
                        static_cast<unsigned long long>(h.magic));
            std::printf("  version     = %u\n", h.version);
            std::printf("  recordSize  = %u\n", h.recordSize);
            std::printf("  bucketCount = %u\n", h.bucketCount);
            std::printf("  headerSize  = %u\n", h.headerSize);
            std::printf("  headerCrc32 = 0x%08X\n", h.headerCrc32);
        } catch (const std::exception& e) {
            std::fprintf(stderr, "[ERR] %s: %s\n", argv[i], e.what());
        }
    }
    return 0;
}

