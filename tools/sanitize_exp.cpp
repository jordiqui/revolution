#include "experience_io.h"

#include <cstdio>
#include <cstdlib>
#include <exception>
#include <string>

int main(int argc, char** argv) {
    if (argc < 2 || argc > 3) {
        std::fprintf(stderr, "usage: %s <file.exp> [buckets]\n", argv[0]);
        return 2;
    }

    const std::string path = argv[1];

    std::uint32_t buckets = kDefaultExperienceBuckets;
    if (argc == 3) {
        try {
            buckets = static_cast<std::uint32_t>(std::stoul(argv[2]));
        } catch (const std::exception&) {
            std::fprintf(stderr, "invalid bucket count: %s\n", argv[2]);
            return 2;
        }
    }

    buckets = Experience_NormalizeBucketCount(buckets);

    try {
        auto result = Experience_OpenForReadWrite(path, buckets);
        if (!result.ok) {
            std::fprintf(stderr, "ERROR: failed to repair %s\n", path.c_str());
            return 1;
        }

        std::printf("OK: %s (buckets=%u, record=%u, version=%u, readonly=%s)\n",
                    result.normalizedPath.c_str(),
                    result.bucketCount,
                    result.recordSize,
                    result.version,
                    result.readOnly ? "yes" : "no");
    } catch (const std::exception& e) {
        std::fprintf(stderr, "ERROR: %s\n", e.what());
        return 1;
    }

    return 0;
}

