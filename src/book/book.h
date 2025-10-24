#ifndef BOOK_H_INCLUDED
#define BOOK_H_INCLUDED

#include <cassert>
#include <cstdint>
#include <cstring>
#include <string>
#include <type_traits>

#include "position.h"
#include "types.h"

namespace Stockfish {

class BookManager;

namespace Book {

namespace Detail {
    inline bool IsBigEndian() {
        const std::uint32_t value = 0x01020304u;
        return reinterpret_cast<const unsigned char*>(&value)[0] == 0x01;
    }
}

class BookUtil {
   public:
    template<typename IntType>
    static IntType read_big_endian(const unsigned char* buffer, size_t& offset, size_t bufferLen) {
        IntType        result;
        constexpr auto typeSize = sizeof(IntType);

        if (offset + typeSize > bufferLen)
        {
            assert(false);
            return IntType();
        }

        std::memcpy(&result, buffer + offset, typeSize);

        if (!Detail::IsBigEndian())
        {
            unsigned char                                          u[typeSize];
            typename std::make_unsigned<IntType>::type             v = 0;

            std::memcpy(&u, &result, typeSize);
            for (size_t i = 0; i < typeSize; ++i)
                v = (v << 8) | u[i];

            std::memcpy(&result, &v, typeSize);
        }

        offset += typeSize;
        return result;
    }

    template<typename IntType>
    static IntType read_big_endian(const unsigned char* buffer, size_t bufferLen) {
        size_t offset = 0;
        return read_big_endian<IntType>(buffer, offset, bufferLen);
    }
};

class Book {
    friend class ::Stockfish::BookManager;

   private:
   static Book* create_book(const std::string& filename);

   public:
    Book()                    = default;
    virtual ~Book() = default;

    Book(const Book&)            = delete;
    Book& operator=(const Book&) = delete;

    virtual std::string type() const = 0;

    virtual bool open(const std::string& filename) = 0;
    virtual void close()                            = 0;

    virtual Move probe(const Position& pos, size_t width, bool onlyGreen) const = 0;
    virtual void show_moves(const Position& pos) const                          = 0;
};

}  // namespace Book

}  // namespace Stockfish

#endif
