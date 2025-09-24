#pragma once

#include <cstdint>
#include <string>

#pragma pack(push, 1)
struct ExperienceHeader {
    std::uint64_t magic;
    std::uint32_t version;
    std::uint32_t recordSize;
    std::uint32_t bucketCount;
    std::uint32_t headerSize;
    std::uint32_t headerCrc32;
    std::uint8_t  reserved[32];
};
#pragma pack(pop)

#pragma pack(push, 1)
struct ExperienceRecord {
    std::uint64_t key;
    std::uint16_t move;
    std::int16_t  score;
    std::int16_t  depth;
    std::int16_t  count;
    std::uint32_t flags;
    std::uint64_t lastWritten;
};
#pragma pack(pop)

static_assert(sizeof(ExperienceHeader) == 64, "ExperienceHeader must remain stable");
static_assert(sizeof(ExperienceRecord) == 28, "ExperienceRecord layout changed unexpectedly");

constexpr std::uint64_t kExperienceMagic   = 0xAABBEEDD66778899ULL;
constexpr std::uint32_t kExperienceVersion = 1;
constexpr std::uint32_t kMinExperienceBuckets = 1u << 10;
constexpr std::uint32_t kMaxExperienceBuckets = 1u << 24;
constexpr std::uint32_t kDefaultExperienceBuckets = 1u << 15;

struct ExperienceOpenResult {
    bool         ok           = false;
    bool         readOnly     = false;
    bool         recreated    = false;
    std::uint32_t bucketCount = 0;
    std::uint32_t recordSize  = 0;
    std::uint32_t version     = 0;
    std::string   normalizedPath;
};

std::uint32_t Experience_NormalizeBucketCount(std::uint32_t requested);

ExperienceOpenResult Experience_OpenForReadWrite(const std::string& path,
                                                 std::uint32_t        requestedBuckets = kDefaultExperienceBuckets);
bool Experience_InitNew(const std::string& path, std::uint32_t bucketCount = kDefaultExperienceBuckets);
bool Experience_IsCompatible(const ExperienceHeader& h);
bool Experience_ReadHeader(const std::string& path, ExperienceHeader& out);
bool Experience_WriteHeader(const std::string& path, const ExperienceHeader& h);

bool Experience_FileLooksLikeSugar(const ExperienceHeader& h);

