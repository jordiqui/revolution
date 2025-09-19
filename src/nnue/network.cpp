/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2025 The Stockfish developers (see AUTHORS file)

  Stockfish is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  Stockfish is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "network.h"

#include <algorithm>
#include <cassert>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <type_traits>
#include <vector>

#define INCBIN_SILENCE_BITCODE_WARNING
#include "../incbin/incbin.h"

#include "../evaluate.h"
#include "../memory.h"
#include "../misc.h"
#include "../position.h"
#include "../types.h"
#include "nnue_architecture.h"
#include "nnue_common.h"
#include "nnue_misc.h"

// Macro to embed the default efficiently updatable neural network (NNUE) file
// data in the engine binary (using incbin.h, by Dale Weiler).
// This macro invocation will declare the following three variables
//     const unsigned char        gEmbeddedNNUEData[];  // a pointer to the embedded data
//     const unsigned char *const gEmbeddedNNUEEnd;     // a marker to the end
//     const unsigned int         gEmbeddedNNUESize;    // the size of the embedded file
// Note that this does not work in Microsoft Visual Studio.
#if !defined(_MSC_VER) && !defined(NNUE_EMBEDDING_OFF)
INCBIN(EmbeddedNNUEBig, EvalFileDefaultNameBig);
INCBIN(EmbeddedNNUESmall, EvalFileDefaultNameSmall);
#  if __has_include(FalconFileDefaultName)
INCBIN(EmbeddedNNUEFalcon, FalconFileDefaultName);
#  else
const unsigned char        gEmbeddedNNUEFalconData[1] = {0x0};
const unsigned char* const gEmbeddedNNUEFalconEnd     = &gEmbeddedNNUEFalconData[1];
const unsigned int         gEmbeddedNNUEFalconSize    = 1;
#  endif
#else
const unsigned char        gEmbeddedNNUEBigData[1]   = {0x0};
const unsigned char* const gEmbeddedNNUEBigEnd       = &gEmbeddedNNUEBigData[1];
const unsigned int         gEmbeddedNNUEBigSize      = 1;
const unsigned char        gEmbeddedNNUESmallData[1] = {0x0};
const unsigned char* const gEmbeddedNNUESmallEnd     = &gEmbeddedNNUESmallData[1];
const unsigned int         gEmbeddedNNUESmallSize    = 1;
const unsigned char        gEmbeddedNNUEFalconData[1] = {0x0};
const unsigned char* const gEmbeddedNNUEFalconEnd     = &gEmbeddedNNUEFalconData[1];
const unsigned int         gEmbeddedNNUEFalconSize    = 1;
#endif

namespace {

struct EmbeddedNNUE {
    EmbeddedNNUE(const unsigned char* embeddedData,
                 const unsigned char* embeddedEnd,
                 const unsigned int   embeddedSize) :
        data(embeddedData),
        end(embeddedEnd),
        size(embeddedSize) {}
    const unsigned char* data;
    const unsigned char* end;
    const unsigned int   size;
};

using namespace Stockfish::Eval::NNUE;

EmbeddedNNUE get_embedded(EmbeddedNNUEType type) {
    if (type == EmbeddedNNUEType::BIG)
        return EmbeddedNNUE(gEmbeddedNNUEBigData, gEmbeddedNNUEBigEnd, gEmbeddedNNUEBigSize);
    else if (type == EmbeddedNNUEType::SMALL)
        return EmbeddedNNUE(gEmbeddedNNUESmallData, gEmbeddedNNUESmallEnd, gEmbeddedNNUESmallSize);
    else
        return EmbeddedNNUE(
          gEmbeddedNNUEFalconData, gEmbeddedNNUEFalconEnd, gEmbeddedNNUEFalconSize);
}

}


namespace Stockfish::Eval::NNUE {


namespace Detail {

// Read evaluation function parameters
template<typename T>
bool read_parameters(std::istream& stream, T& reference) {

    std::uint32_t header;
    header = read_little_endian<std::uint32_t>(stream);
    if (!stream || header != T::get_hash_value())
        return false;
    return reference.read_parameters(stream);
}

// Write evaluation function parameters
template<typename T>
bool write_parameters(std::ostream& stream, T& reference) {

    write_little_endian<std::uint32_t>(stream, T::get_hash_value());
    return reference.write_parameters(stream);
}

}  // namespace Detail

template<typename Arch, typename Transformer>
Network<Arch, Transformer>::Weights::Weights() :
    featureTransformer(make_unique_large_page<Transformer>()),
    network(make_unique_aligned<Arch[]>(LayerStacks)) {}

template<typename Arch, typename Transformer>
Network<Arch, Transformer>::Network(const Network<Arch, Transformer>& other) :
    weights(std::atomic_load(&other.weights)),
    evalFile(other.evalFile),
    embeddedType(other.embeddedType) {}

template<typename Arch, typename Transformer>
Network<Arch, Transformer>&
Network<Arch, Transformer>::operator=(const Network<Arch, Transformer>& other) {
    if (this == &other)
        return *this;

    evalFile     = other.evalFile;
    embeddedType = other.embeddedType;

    std::atomic_store(&weights, std::atomic_load(&other.weights));

    return *this;
}

template<typename Arch, typename Transformer>
void Network<Arch, Transformer>::load(const std::string& rootDirectory, std::string evalfilePath) {
#if defined(DEFAULT_NNUE_DIRECTORY)
    std::vector<std::string> dirs = {"<internal>", "", rootDirectory,
                                     stringify(DEFAULT_NNUE_DIRECTORY)};
#else
    std::vector<std::string> dirs = {"<internal>", "", rootDirectory};
#endif

    if (evalfilePath.empty())
        evalfilePath = evalFile.defaultName;

    for (const auto& directory : dirs)
    {
        if (evalFile.current != evalfilePath)
        {
            if (directory != "<internal>")
            {
                load_user_net(directory, evalfilePath);
            }

            if (directory == "<internal>" && evalfilePath == evalFile.defaultName)
            {
                load_internal();
            }
        }
    }
}


template<typename Arch, typename Transformer>
bool Network<Arch, Transformer>::save(const std::optional<std::string>& filename) const {
    std::string actualFilename;
    std::string msg;

    if (filename.has_value())
        actualFilename = filename.value();
    else
    {
        if (evalFile.current != evalFile.defaultName)
        {
            msg = "Failed to export a net. "
                  "A non-embedded net can only be saved if the filename is specified";

            sync_cout << msg << sync_endl;
            return false;
        }

        actualFilename = evalFile.defaultName;
    }

    std::ofstream stream(actualFilename, std::ios_base::binary);
    auto          storage = weights_handle();

    if (!storage)
    {
        msg = "Failed to export a net";
        sync_cout << msg << sync_endl;
        return false;
    }

    bool saved = save(stream, evalFile.current, evalFile.netDescription, *storage);

    msg = saved ? "Network saved successfully to " + actualFilename : "Failed to export a net";

    sync_cout << msg << sync_endl;
    return saved;
}


template<typename Arch, typename Transformer>
typename Network<Arch, Transformer>::WeightsPtr
Network<Arch, Transformer>::weights_handle() const {
    return std::atomic_load(&weights);
}


template<typename Arch, typename Transformer>
NetworkOutput
Network<Arch, Transformer>::evaluate(const Position&                         pos,
                                     AccumulatorStack&                       accumulatorStack,
                                     AccumulatorCaches::Cache<FTDimensions>* cache) const {

    constexpr uint64_t alignment = CacheLineSize;

    alignas(alignment)
      TransformedFeatureType transformedFeatures[FeatureTransformer<FTDimensions>::BufferSize];

    ASSERT_ALIGNED(transformedFeatures, alignment);

    auto storage = weights_handle();
    if (!storage)
    {
        assert(storage && "NNUE weights not initialized");
        return {Value(0), Value(0)};
    }

    const int  bucket = (pos.count<ALL_PIECES>() - 1) / 4;
    const auto psqt = storage->featureTransformer->transform(pos, accumulatorStack, cache,
                                                             transformedFeatures, bucket);
    const auto positional = storage->network[bucket].propagate(transformedFeatures);
    return {static_cast<Value>(psqt / OutputScale), static_cast<Value>(positional / OutputScale)};
}


template<typename Arch, typename Transformer>
void Network<Arch, Transformer>::verify(std::string                                  evalfilePath,
                                        const std::function<void(std::string_view)>& f) const {
    if (evalfilePath.empty())
        evalfilePath = evalFile.defaultName;

    if (evalFile.current != evalfilePath)
    {
        if (f)
        {
            std::string msg1 =
              "Network evaluation parameters compatible with the engine must be available.";
            std::string msg2 = "The network file " + evalfilePath + " was not loaded successfully.";
            std::string msg3 = "The UCI option EvalFile might need to specify the full path, "
                               "including the directory name, to the network file.";
            std::string msg4 = "The default net can be downloaded from: "
                               "https://tests.stockfishchess.org/api/nn/"
                             + evalFile.defaultName;
            std::string msg5 = "The engine will be terminated now.";

            std::string msg = "ERROR: " + msg1 + '\n' + "ERROR: " + msg2 + '\n' + "ERROR: " + msg3
                            + '\n' + "ERROR: " + msg4 + '\n' + "ERROR: " + msg5 + '\n';

            f(msg);
        }

        exit(EXIT_FAILURE);
    }

    if (f)
    {
        auto storage = weights_handle();
        if (!storage)
            return;

        size_t size = sizeof(*storage->featureTransformer) + sizeof(Arch) * LayerStacks;
        f("NNUE evaluation using " + evalfilePath + " (" + std::to_string(size / (1024 * 1024))
          + "MiB, (" + std::to_string(storage->featureTransformer->InputDimensions) + ", "
          + std::to_string(storage->network[0].TransformedFeatureDimensions) + ", "
          + std::to_string(storage->network[0].FC_0_OUTPUTS) + ", "
          + std::to_string(storage->network[0].FC_1_OUTPUTS) + ", 1))");
    }
}


template<typename Arch, typename Transformer>
bool Network<Arch, Transformer>::verify_optional(
  std::string evalfilePath, const std::function<void(std::string_view)>& f) const {
    if (evalfilePath.empty())
        evalfilePath = evalFile.defaultName;

    std::string normalized = evalfilePath;
    std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    const bool requested = !normalized.empty() && normalized != "none";

    if (evalFile.current != evalfilePath || evalFile.current == "None")
    {
        if (requested && f)
        {
            f("WARNING: Optional network file " + evalfilePath
              + " was not loaded; continuing without Falcon evaluations.");
        }
        return false;
    }

    if (f)
    {
        auto storage = weights_handle();
        if (!storage)
            return true;

        size_t size = sizeof(*storage->featureTransformer) + sizeof(Arch) * LayerStacks;
        f("NNUE evaluation using " + evalfilePath + " (" + std::to_string(size / (1024 * 1024))
          + "MiB, (" + std::to_string(storage->featureTransformer->InputDimensions) + ", "
          + std::to_string(storage->network[0].TransformedFeatureDimensions) + ", "
          + std::to_string(storage->network[0].FC_0_OUTPUTS) + ", "
          + std::to_string(storage->network[0].FC_1_OUTPUTS) + ", 1))");
    }

    return true;
}


template<typename Arch, typename Transformer>
NnueEvalTrace
Network<Arch, Transformer>::trace_evaluate(const Position&                         pos,
                                           AccumulatorStack&                       accumulatorStack,
                                           AccumulatorCaches::Cache<FTDimensions>* cache) const {

    constexpr uint64_t alignment = CacheLineSize;

    alignas(alignment)
      TransformedFeatureType transformedFeatures[FeatureTransformer<FTDimensions>::BufferSize];

    ASSERT_ALIGNED(transformedFeatures, alignment);

    NnueEvalTrace t{};
    t.correctBucket = (pos.count<ALL_PIECES>() - 1) / 4;
    auto storage    = weights_handle();
    if (!storage)
    {
        assert(storage && "NNUE weights not initialized");
        return t;
    }
    for (IndexType bucket = 0; bucket < LayerStacks; ++bucket)
    {
        const auto materialist = storage->featureTransformer->transform(pos, accumulatorStack, cache,
                                                                        transformedFeatures, bucket);
        const auto positional = storage->network[bucket].propagate(transformedFeatures);

        t.psqt[bucket]       = static_cast<Value>(materialist / OutputScale);
        t.positional[bucket] = static_cast<Value>(positional / OutputScale);
    }

    return t;
}


template<typename Arch, typename Transformer>
void Network<Arch, Transformer>::load_user_net(const std::string& dir,
                                               const std::string& evalfilePath) {
    std::ifstream stream(dir + evalfilePath, std::ios::binary);
    auto          description = load(stream);

    if (description.has_value())
    {
        evalFile.current        = evalfilePath;
        evalFile.netDescription = description.value();
    }
}


template<typename Arch, typename Transformer>
void Network<Arch, Transformer>::load_internal() {
    // C++ way to prepare a buffer for a memory stream
    class MemoryBuffer: public std::basic_streambuf<char> {
       public:
        MemoryBuffer(char* p, size_t n) {
            setg(p, p, p + n);
            setp(p, p + n);
        }
    };

    const auto embedded = get_embedded(embeddedType);

    MemoryBuffer buffer(const_cast<char*>(reinterpret_cast<const char*>(embedded.data)),
                        size_t(embedded.size));

    std::istream stream(&buffer);
    auto         description = load(stream);

    if (description.has_value())
    {
        evalFile.current        = evalFile.defaultName;
        evalFile.netDescription = description.value();
    }
}


template<typename Arch, typename Transformer>
typename Network<Arch, Transformer>::WeightsPtr
Network<Arch, Transformer>::allocate_weights() const {
    return std::make_shared<Weights>();
}


template<typename Arch, typename Transformer>
bool Network<Arch, Transformer>::save(std::ostream&      stream,
                                      const std::string& name,
                                      const std::string& netDescription,
                                      const Weights&     storage) const {
    if (name.empty() || name == "None")
        return false;

    return write_parameters(stream, netDescription, storage);
}


template<typename Arch, typename Transformer>
std::optional<std::string> Network<Arch, Transformer>::load(std::istream& stream) {
    std::string description;

    auto newWeights = allocate_weights();

    if (!read_parameters(stream, description, *newWeights))
        return std::nullopt;

    std::atomic_store(&weights, std::move(newWeights));

    return std::make_optional(description);
}


// Read network header
template<typename Arch, typename Transformer>
bool Network<Arch, Transformer>::read_header(std::istream&  stream,
                                             std::uint32_t* hashValue,
                                             std::string*   desc) const {
    std::uint32_t version, size;

    version    = read_little_endian<std::uint32_t>(stream);
    *hashValue = read_little_endian<std::uint32_t>(stream);
    size       = read_little_endian<std::uint32_t>(stream);
    if (!stream || version != Version)
        return false;
    desc->resize(size);
    stream.read(&(*desc)[0], size);
    return !stream.fail();
}


// Write network header
template<typename Arch, typename Transformer>
bool Network<Arch, Transformer>::write_header(std::ostream&      stream,
                                              std::uint32_t      hashValue,
                                              const std::string& desc) const {
    write_little_endian<std::uint32_t>(stream, Version);
    write_little_endian<std::uint32_t>(stream, hashValue);
    write_little_endian<std::uint32_t>(stream, std::uint32_t(desc.size()));
    stream.write(&desc[0], desc.size());
    return !stream.fail();
}


template<typename Arch, typename Transformer>
bool Network<Arch, Transformer>::read_parameters(std::istream& stream,
                                                 std::string&  netDescription,
                                                 Weights&      storage) const {
    std::uint32_t hashValue;
    if (!read_header(stream, &hashValue, &netDescription))
        return false;
    if (hashValue != Network::hash)
        return false;
    if (!Detail::read_parameters(stream, *storage.featureTransformer))
        return false;
    for (std::size_t i = 0; i < LayerStacks; ++i)
    {
        if (!Detail::read_parameters(stream, storage.network[i]))
            return false;
    }
    return stream && stream.peek() == std::ios::traits_type::eof();
}


template<typename Arch, typename Transformer>
bool Network<Arch, Transformer>::write_parameters(std::ostream&      stream,
                                                  const std::string& netDescription,
                                                  const Weights&     storage) const {
    if (!write_header(stream, Network::hash, netDescription))
        return false;
    if (!Detail::write_parameters(stream, *storage.featureTransformer))
        return false;
    for (std::size_t i = 0; i < LayerStacks; ++i)
    {
        if (!Detail::write_parameters(stream, storage.network[i]))
            return false;
    }
    return bool(stream);
}

// Explicit template instantiations

template class Network<NetworkArchitecture<TransformedFeatureDimensionsBig, L2Big, L3Big>,
                       FeatureTransformer<TransformedFeatureDimensionsBig>>;

template class Network<NetworkArchitecture<TransformedFeatureDimensionsSmall, L2Small, L3Small>,
                       FeatureTransformer<TransformedFeatureDimensionsSmall>>;

}  // namespace Stockfish::Eval::NNUE
