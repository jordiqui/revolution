/*
  Revolution, a UCI chess playing engine derived from Stockfish 17.1
  Copyright (C) 2004-2025 The Stockfish developers (see AUTHORS file)

  Revolution is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  Revolution is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "memory.h"

#include <cstdlib>
#include <fstream>
#include <limits>
#include <string>

#if __has_include("features.h")
    #include <features.h>
#endif

#if defined(__linux__) && !defined(__ANDROID__)
    #include <sys/mman.h>
#endif

#if defined(__APPLE__) || defined(__ANDROID__) || defined(__OpenBSD__) \
  || (defined(__GLIBCXX__) && !defined(_GLIBCXX_HAVE_ALIGNED_ALLOC) && !defined(_WIN32)) \
  || defined(__e2k__)
    #define POSIXALIGNEDALLOC
    #include <stdlib.h>
#endif

#ifdef _WIN32
    #if _WIN32_WINNT < 0x0601
        #undef _WIN32_WINNT
        #define _WIN32_WINNT 0x0601  // Force to include needed API prototypes
    #endif

    #ifndef NOMINMAX
        #define NOMINMAX
    #endif

    #include <ios>       // std::hex, std::dec
    #include <iostream>  // std::cerr
    #include <ostream>   // std::endl
    #include <windows.h>

// The needed Windows API for processor groups could be missed from old Windows
// versions, so instead of calling them directly (forcing the linker to resolve
// the calls at compile time), try to load them at runtime. To do this we need
// first to define the corresponding function pointers.

namespace Stockfish {

// Wrappers for systems where the c++17 implementation does not guarantee the
// availability of aligned_alloc(). Memory allocated with std_aligned_alloc()
// must be freed with std_aligned_free().

namespace {

size_t adjust_size_for_alignment(size_t alignment, size_t size, bool& overflowed) {
    overflowed = false;

    if (!alignment)
        return size;

    const size_t remainder = size % alignment;
    if (!remainder)
        return size;

    const size_t increment = alignment - remainder;
    if (increment > std::numeric_limits<size_t>::max() - size)
    {
        overflowed = true;
        return 0;
    }

    return size + increment;
}

}  // namespace

void* std_aligned_alloc(size_t alignment, size_t size) {
    bool   overflowed   = false;
    size_t adjustedSize = adjust_size_for_alignment(alignment, size, overflowed);
    if (overflowed)
        return nullptr;

#if defined(_ISOC11_SOURCE)
    return aligned_alloc(alignment, adjustedSize);
#elif defined(POSIXALIGNEDALLOC)
    void* mem = nullptr;
    posix_memalign(&mem, alignment, adjustedSize);
    return mem;
#elif defined(_WIN32) && !defined(_M_ARM) && !defined(_M_ARM64)
    return _mm_malloc(adjustedSize, alignment);
#elif defined(_WIN32)
    return _aligned_malloc(adjustedSize, alignment);
#else
    return std::aligned_alloc(alignment, adjustedSize);
#endif
}

void std_aligned_free(void* ptr) {

#if defined(POSIXALIGNEDALLOC)
    free(ptr);
#elif defined(_WIN32) && !defined(_M_ARM) && !defined(_M_ARM64)
    _mm_free(ptr);
#elif defined(_WIN32)
    _aligned_free(ptr);
#else
    free(ptr);
#endif
}

// aligned_large_pages_alloc() will return suitably aligned memory,
// if possible using large pages.

#if defined(_WIN32)

static void* aligned_large_pages_alloc_windows([[maybe_unused]] size_t allocSize) {

    return windows_try_with_large_page_priviliges(
      [&](size_t largePageSize) {
          // Round up size to full pages and allocate
          allocSize = (allocSize + largePageSize - 1) & ~size_t(largePageSize - 1);
          return VirtualAlloc(nullptr, allocSize, MEM_RESERVE | MEM_COMMIT | MEM_LARGE_PAGES,
                              PAGE_READWRITE);
      },
      []() { return (void*) nullptr; });
}

void* aligned_large_pages_alloc(size_t allocSize) {

    // Try to allocate large pages
    void* mem = aligned_large_pages_alloc_windows(allocSize);

    // Fall back to regular, page-aligned, allocation if necessary
    if (!mem)
        mem = VirtualAlloc(nullptr, allocSize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);

    return mem;
}

#else

void* aligned_large_pages_alloc(size_t allocSize) {

    #if defined(__linux__)
    constexpr size_t alignment = 2 * 1024 * 1024;  // 2MB page size assumed
    #else
    constexpr size_t alignment = 4096;  // small page size assumed
    #endif

    // Round up to multiples of alignment
    size_t size = ((allocSize + alignment - 1) / alignment) * alignment;
    void*  mem  = std_aligned_alloc(alignment, size);
    #if defined(MADV_HUGEPAGE)
    madvise(mem, size, MADV_HUGEPAGE);
    #endif
    return mem;
}

#endif

bool has_large_pages() {

#if defined(_WIN32)

    constexpr size_t page_size = 2 * 1024 * 1024;  // 2MB page size assumed
    void*            mem       = aligned_large_pages_alloc_windows(page_size);
    if (mem == nullptr)
    {
        return false;
    }
    else
    {
        aligned_large_pages_free(mem);
        return true;
    }

#elif defined(__linux__)

    #if defined(MADV_HUGEPAGE)
    auto transparent_hugepages_enabled = []() {
        std::ifstream thp("/sys/kernel/mm/transparent_hugepage/enabled");
        if (!thp.is_open())
            return false;

        std::string line;
        std::getline(thp, line);
        return line.find("[always]") != std::string::npos
            || line.find("[madvise]") != std::string::npos;
    };

    if (transparent_hugepages_enabled())
        return true;

    std::ifstream meminfo("/proc/meminfo");
    if (!meminfo.is_open())
        return false;

    std::string key;
    long        value = 0;
    std::string unit;
    bool        hasHugePages  = false;
    bool        hasFreePages  = false;

    while (meminfo >> key >> value >> unit)
    {
        if (key == "HugePages_Total:" && value > 0)
            hasHugePages = true;
        else if (key == "HugePages_Free:" && value > 0)
            hasFreePages = true;

        if (hasHugePages && hasFreePages)
            return true;
    }

    return false;
    #else
    return false;
    #endif

#else

    return false;

#endif
}


// aligned_large_pages_free() will free the previously memory allocated
// by aligned_large_pages_alloc(). The effect is a nop if mem == nullptr.

#if defined(_WIN32)

void aligned_large_pages_free(void* mem) {

    if (mem && !VirtualFree(mem, 0, MEM_RELEASE))
    {
        DWORD err = GetLastError();
        std::cerr << "Failed to free large page memory. Error code: 0x" << std::hex << err
                  << std::dec << std::endl;
        exit(EXIT_FAILURE);
    }
}

#else

void aligned_large_pages_free(void* mem) { std_aligned_free(mem); }

#endif
}  // namespace Stockfish
