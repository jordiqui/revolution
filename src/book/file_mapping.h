#ifndef FILEMAPPING_H_INCLUDED
#define FILEMAPPING_H_INCLUDED

#include <cstddef>
#include <cstdint>
#include <string>

namespace Stockfish::Book {

class FileMapping {
   public:
    FileMapping();
    ~FileMapping();

    bool map(const std::string& f, bool verbose);
    void unmap();

    bool                  has_data() const;
    const unsigned char*  data() const;
    size_t                data_size() const;

   private:
    std::uint64_t mapping;
    void*         baseAddress;
    size_t        dataSize;
};

}  // namespace Stockfish::Book

#endif  // #ifndef FILEMAPPING_H_INCLUDED
