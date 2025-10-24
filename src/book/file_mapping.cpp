#include "book/file_mapping.h"

#include "misc.h"

#ifndef _WIN32
#    include <fcntl.h>
#    include <sys/mman.h>
#    include <sys/stat.h>
#    include <unistd.h>
#else
#    define WIN32_LEAN_AND_MEAN
#    ifndef NOMINMAX
#        define NOMINMAX
#    endif
#    include <windows.h>
#endif

#include <cassert>
#include <iostream>

namespace Stockfish::Book {

FileMapping::FileMapping() : mapping(0), baseAddress(nullptr), dataSize(0) {}

FileMapping::~FileMapping() { unmap(); }

bool FileMapping::map(const std::string& f, bool verbose) {
    unmap();

#ifdef _WIN32
    HANDLE fd = CreateFile(
      f.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_RANDOM_ACCESS, nullptr);

    if (fd == INVALID_HANDLE_VALUE)
    {
        if (verbose)
            sync_cout << "info string CreateFile() failed for: " << f << ". Error code: " << GetLastError() << sync_endl;

        return false;
    }

    LARGE_INTEGER li{};
    if (!GetFileSizeEx(fd, &li) || li.QuadPart == 0)
    {
        CloseHandle(fd);

        if (verbose)
            sync_cout << "info string File is empty: " << f << sync_endl;

        return false;
    }

    HANDLE mmap = CreateFileMapping(fd, nullptr, PAGE_READONLY, li.HighPart, li.LowPart, nullptr);
    CloseHandle(fd);

    if (!mmap)
    {
        if (verbose)
            sync_cout << "info string CreateFileMapping() failed for: " << f << ". Error code: " << GetLastError()
                      << sync_endl;

        return false;
    }

    void* viewBase = MapViewOfFile(mmap, FILE_MAP_READ, 0, 0, 0);
    if (!viewBase)
    {
        if (verbose)
            sync_cout << "info string MapViewOfFile() failed for: " << f << ". Error code: " << GetLastError() << sync_endl;

        CloseHandle(mmap);
        return false;
    }

    mapping     = std::uint64_t(mmap);
    baseAddress = viewBase;
    dataSize    = size_t(li.QuadPart);
#else
    struct stat statbuf {};
    int         fd = ::open(f.c_str(), O_RDONLY);

    if (fd == -1)
    {
        if (verbose)
            sync_cout << "info string open() failed for: " << f << sync_endl;

        return false;
    }

    fstat(fd, &statbuf);
    if (statbuf.st_size == 0)
    {
        ::close(fd);

        if (verbose)
            sync_cout << "info string File is empty: " << f << sync_endl;

        return false;
    }

    void* data = mmap(nullptr, statbuf.st_size, PROT_READ, MAP_SHARED, fd, 0);
    if (data == MAP_FAILED)
    {
        ::close(fd);

        if (verbose)
            sync_cout << "info string mmap() failed for: " << f << sync_endl;

        return false;
    }

#    if defined(MADV_RANDOM)
    madvise(data, statbuf.st_size, MADV_RANDOM);
#    endif
    ::close(fd);

    mapping     = statbuf.st_size;
    baseAddress = data;
    dataSize    = statbuf.st_size;
#endif

    return true;
}

void FileMapping::unmap() {
    assert((mapping == 0) == (baseAddress == nullptr) && (baseAddress == nullptr) == (dataSize == 0));

#ifdef _WIN32
    if (baseAddress)
        UnmapViewOfFile(baseAddress);

    if (mapping)
        CloseHandle(reinterpret_cast<HANDLE>(mapping));
#else
    if (baseAddress && mapping)
        munmap(baseAddress, mapping);
#endif

    baseAddress = nullptr;
    mapping     = 0;
    dataSize    = 0;
}

bool FileMapping::has_data() const {
    assert((mapping == 0) == (baseAddress == nullptr) && (baseAddress == nullptr) == (dataSize == 0));
    return baseAddress != nullptr && dataSize != 0;
}

const unsigned char* FileMapping::data() const {
    assert(mapping != 0 && baseAddress != nullptr && dataSize != 0);
    return static_cast<const unsigned char*>(baseAddress);
}

size_t FileMapping::data_size() const {
    assert(mapping != 0 && baseAddress != nullptr && dataSize != 0);
    return dataSize;
}

}  // namespace Stockfish::Book
