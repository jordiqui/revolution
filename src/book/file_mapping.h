#ifndef FILEMAPPING_H_INCLUDED
#define FILEMAPPING_H_INCLUDED

#include <cstddef>
#include <cstdint>
#include <string>

namespace Stockfish {
namespace Book {

class FileMapping {
   private:
    std::uint64_t mapping;
    void*         baseAddress;
    std::size_t   dataSize;

   public:
    FileMapping();
    ~FileMapping();

    bool map(const std::string& f, bool verbose);
    void unmap();

    bool                 has_data() const;
    const unsigned char* data() const;
    std::size_t          data_size() const;
};

}  // namespace Book
}  // namespace Stockfish

#endif  // FILEMAPPING_H_INCLUDED
