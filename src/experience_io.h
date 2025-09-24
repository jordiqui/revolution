#pragma once

#include <cstdint>
#include <string>

struct ExpHeader {
    std::uint32_t magic;
    std::uint32_t ver_flags;
    std::uint32_t field3;
    std::uint32_t field4;
    std::uint32_t record_size;
    std::uint32_t marker;
};

constexpr std::uint32_t kMagic_Accepted1 = 0x562F59FB;
constexpr std::uint32_t kEngineRecordSize = 796;
constexpr std::uint32_t kDefaultVerFlags  = 0x8F8F01D4;
constexpr std::uint32_t kDefaultField3    = 20;
constexpr std::uint32_t kDefaultField4    = 123;
constexpr std::uint32_t kDefaultMarker    = 1;

bool Experience_OpenForReadWrite(const std::string& path);
bool Experience_InitNew(const std::string& path);
bool Experience_IsCompatible(const ExpHeader& h);
bool Experience_ReadHeader(const std::string& path, ExpHeader& out);
bool Experience_WriteHeader(const std::string& path, const ExpHeader& h);

bool Experience_FileLooksLikeSugar(const ExpHeader& h);

