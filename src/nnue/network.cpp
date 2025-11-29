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

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <optional>
#include <sstream>
#include <type_traits>
#include <vector>

#define INCBIN_SILENCE_BITCODE_WARNING
#include "../incbin/incbin.h"

#include "../evaluate.h"
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
#else
const unsigned char        gEmbeddedNNUEBigData[1]   = {0x0};
const unsigned char* const gEmbeddedNNUEBigEnd       = &gEmbeddedNNUEBigData[1];
const unsigned int         gEmbeddedNNUEBigSize      = 1;
const unsigned char        gEmbeddedNNUESmallData[1] = {0x0};
const unsigned char* const gEmbeddedNNUESmallEnd     = &gEmbeddedNNUESmallData[1];
const unsigned int         gEmbeddedNNUESmallSize    = 1;
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
    else
        return EmbeddedNNUE(gEmbeddedNNUESmallData, gEmbeddedNNUESmallEnd, gEmbeddedNNUESmallSize);
}

}


namespace Stockfish::Eval::NNUE {

bool NetMetadata::compatible() const {
    const bool versionSupported = version == Version || version == ExtendedVersion;
    const bool quantizationSupported =
      quantization.empty() || quantization == "int16" || quantization == "s8" || quantization == "int8"
      || quantization == "s16";

    return versionSupported && quantizationSupported;
}


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
bool write_parameters(std::ostream& stream, const T& reference) {

    write_little_endian<std::uint32_t>(stream, T::get_hash_value());
    return reference.write_parameters(stream);
}

}  // namespace Detail

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

    bool loaded = false;

    for (const auto& directory : dirs)
    {
        if (std::string(evalFile.current) != evalfilePath)
        {
            if (directory != "<internal>")
            {
                auto result = load_user_net(directory, evalfilePath);
                loaded      = result.loaded && result.compatible;
            }

            if (directory == "<internal>" && evalfilePath == std::string(evalFile.defaultName))
            {
                auto result = load_internal();
                loaded      = loaded || (result.loaded && result.compatible);
            }

            if (loaded)
                break;
        }
    }

    if (!loaded && evalfilePath != std::string(evalFile.defaultName))
    {
        auto fallback = load_internal();
        if (fallback.loaded && fallback.compatible)
        {
            loaded = true;
            sync_cout << "WARNING: External network '" << evalfilePath
                      << "' is not compatible (version " << std::hex << fallback.metadata.version
                      << std::dec << "). Falling back to embedded network '" << evalFile.defaultName
                      << "'." << sync_endl;
            lastAttempt = fallback;
        }
    }

    if (!loaded)
    {
        sync_cout << "WARNING: Unable to load network file '" << evalfilePath
                  << "'. Falling back to a zeroed placeholder network." << sync_endl;

        use_dummy_network(evalfilePath);
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
        if (std::string(evalFile.current) != std::string(evalFile.defaultName))
        {
            msg = "Failed to export a net. "
                  "A non-embedded net can only be saved if the filename is specified";

            sync_cout << msg << sync_endl;
            return false;
        }

        actualFilename = evalFile.defaultName;
    }

    std::ofstream stream(actualFilename, std::ios_base::binary);
    bool          saved = save(stream, evalFile.current, evalFile.netDescription);

    msg = saved ? "Network saved successfully to " + actualFilename : "Failed to export a net";

    sync_cout << msg << sync_endl;
    return saved;
}


template<typename Arch, typename Transformer>
NetworkOutput
Network<Arch, Transformer>::evaluate(const Position&                         pos,
                                     AccumulatorStack&                       accumulatorStack,
                                     AccumulatorCaches::Cache<FTDimensions>& cache) const {

    constexpr uint64_t alignment = CacheLineSize;

    alignas(alignment)
      TransformedFeatureType transformedFeatures[FeatureTransformer<FTDimensions>::BufferSize];

    ASSERT_ALIGNED(transformedFeatures, alignment);

    const int  bucket = (pos.count<ALL_PIECES>() - 1) / 4;
    const auto psqt =
      featureTransformer.transform(pos, accumulatorStack, &cache, transformedFeatures, bucket);
    const auto positional = network[bucket].propagate(transformedFeatures);
    return {static_cast<Value>(psqt / OutputScale), static_cast<Value>(positional / OutputScale)};
}


template<typename Arch, typename Transformer>
void Network<Arch, Transformer>::verify(std::string                                  evalfilePath,
                                        const std::function<void(std::string_view)>& f) const {
    if (evalfilePath.empty())
        evalfilePath = evalFile.defaultName;

    if (std::string(evalFile.current) != evalfilePath)
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
                             + std::string(evalFile.defaultName);
            std::string msg5 = "The engine will be terminated now.";

            std::string msg = "ERROR: " + msg1 + '\n' + "ERROR: " + msg2 + '\n' + "ERROR: " + msg3
                            + '\n' + "ERROR: " + msg4 + '\n' + "ERROR: " + msg5 + '\n';

            f(msg);
        }

        exit(EXIT_FAILURE);
    }

    if (f)
    {
        size_t size = sizeof(featureTransformer) + sizeof(Arch) * LayerStacks;
        f("NNUE evaluation using " + evalfilePath + " (" + std::to_string(size / (1024 * 1024))
          + "MiB, (" + std::to_string(featureTransformer.TotalInputDimensions) + ", "
          + std::to_string(network[0].TransformedFeatureDimensions) + ", "
          + std::to_string(network[0].FC_0_OUTPUTS) + ", " + std::to_string(network[0].FC_1_OUTPUTS)
          + ", 1))");
    }
}


template<typename Arch, typename Transformer>
NnueEvalTrace
Network<Arch, Transformer>::trace_evaluate(const Position&                         pos,
                                           AccumulatorStack&                       accumulatorStack,
                                           AccumulatorCaches::Cache<FTDimensions>& cache) const {

    constexpr uint64_t alignment = CacheLineSize;

    alignas(alignment)
      TransformedFeatureType transformedFeatures[FeatureTransformer<FTDimensions>::BufferSize];

    ASSERT_ALIGNED(transformedFeatures, alignment);

    NnueEvalTrace t{};
    t.correctBucket = (pos.count<ALL_PIECES>() - 1) / 4;
    for (IndexType bucket = 0; bucket < LayerStacks; ++bucket)
    {
        const auto materialist = featureTransformer.transform(pos, accumulatorStack, &cache,
                                                             transformedFeatures, bucket);
        const auto positional = network[bucket].propagate(transformedFeatures);

        t.psqt[bucket]       = static_cast<Value>(materialist / OutputScale);
        t.positional[bucket] = static_cast<Value>(positional / OutputScale);
    }

    return t;
}


template<typename Arch, typename Transformer>
LoadedNetworkInfo Network<Arch, Transformer>::load_user_net(const std::string& dir,
                                                           const std::string& evalfilePath) {
    std::ifstream   stream(dir + evalfilePath, std::ios::binary);
    LoadedNetworkInfo info = load(stream);

    if (info.loaded && info.compatible)
    {
        evalFile.current        = evalfilePath;
        evalFile.netDescription = info.description;
        evalFile.version        = info.metadata.version;
        evalFile.extendedHeader = info.metadata.extendedHeader;
        evalFile.quantization   = info.metadata.quantization;
        evalFile.format         = info.metadata.format;
    }
    else if (info.loaded && !info.compatible)
    {
        sync_cout << "WARNING: Network '" << evalfilePath
                  << "' uses unsupported format (version " << std::hex
                  << info.metadata.version << std::dec << ")" << sync_endl;
    }

    return info;
}


template<typename Arch, typename Transformer>
LoadedNetworkInfo Network<Arch, Transformer>::load_internal() {
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
    auto         info = load(stream);

    if (info.loaded && info.compatible)
    {
        evalFile.current        = evalFile.defaultName;
        evalFile.netDescription = info.description;
        evalFile.version        = info.metadata.version;
        evalFile.extendedHeader = info.metadata.extendedHeader;
        evalFile.quantization   = info.metadata.quantization;
        evalFile.format         = info.metadata.format;
    }

    return info;
}


template<typename Arch, typename Transformer>
void Network<Arch, Transformer>::use_dummy_network(const std::string& evalfilePath) {
    initialize();

    featureTransformer = Transformer{};
    for (auto& layerstack : network)
        layerstack = Arch{};

    evalFile.current        = evalfilePath;
    evalFile.netDescription = "Zero placeholder network";
    evalFile.version        = 0;
    evalFile.extendedHeader = false;
    evalFile.quantization   = "";
    evalFile.format         = "";
}


template<typename Arch, typename Transformer>
void Network<Arch, Transformer>::initialize() {
    initialized = true;
}


template<typename Arch, typename Transformer>
bool Network<Arch, Transformer>::save(std::ostream&      stream,
                                      const std::string& name,
                                      const std::string& netDescription) const {
    if (name.empty() || name == "None")
        return false;

    return write_parameters(stream, netDescription);
}


template<typename Arch, typename Transformer>
LoadedNetworkInfo Network<Arch, Transformer>::load(std::istream& stream) {
    initialize();
    LoadedNetworkInfo info{};

    if (!stream)
        return info;

    read_parameters(stream, info);
    return info;
}


template<typename Arch, typename Transformer>
std::size_t Network<Arch, Transformer>::get_content_hash() const {
    if (!initialized)
        return 0;

    std::size_t h = 0;
    hash_combine(h, featureTransformer);
    for (auto&& layerstack : network)
        hash_combine(h, layerstack);
    hash_combine(h, evalFile);
    hash_combine(h, static_cast<int>(embeddedType));
    return h;
}

// Read network header
template<typename Arch, typename Transformer>
bool Network<Arch, Transformer>::read_header(std::istream&  stream,
                                             std::uint32_t* hashValue,
                                             std::string*   desc,
                                             NetMetadata&   metadata) const {
    std::uint32_t version, size;

    version           = read_little_endian<std::uint32_t>(stream);
    *hashValue        = read_little_endian<std::uint32_t>(stream);
    size              = read_little_endian<std::uint32_t>(stream);
    metadata.version  = version & ~ExtendedVersionFlag;
    metadata.extendedHeader = (version & ExtendedVersionFlag) != 0u;

    if (!stream)
        return false;

    desc->resize(size);
    stream.read(&(*desc)[0], size);

    if (!stream)
        return false;

    if (metadata.extendedHeader)
    {
        std::uint32_t extendedSize = read_little_endian<std::uint32_t>(stream);
        if (!stream)
            return false;

        if (extendedSize > 0 && extendedSize <= 4096)
        {
            std::string extendedPayload(extendedSize, '\0');
            stream.read(extendedPayload.data(), extendedPayload.size());

            std::istringstream metaStream(extendedPayload);
            std::string        line;
            while (std::getline(metaStream, line))
            {
                auto separator = line.find('=');
                if (separator == std::string::npos)
                    continue;

                auto key   = line.substr(0, separator);
                auto value = line.substr(separator + 1);

                if (key == "quantization")
                    metadata.quantization = value;
                else if (key == "format")
                    metadata.format = value;
            }
        }
    }

    return true;
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
bool Network<Arch, Transformer>::read_parameters(std::istream& stream, LoadedNetworkInfo& info) {
    std::uint32_t hashValue;
    if (!read_header(stream, &hashValue, &info.description, info.metadata))
        return false;

    info.compatible = info.metadata.compatible() && hashValue == Network::hash;
    if (!info.compatible)
        return false;

    if (!Detail::read_parameters(stream, featureTransformer))
        return false;
    for (std::size_t i = 0; i < LayerStacks; ++i)
    {
        if (!Detail::read_parameters(stream, network[i]))
            return false;
    }

    info.loaded = stream && stream.peek() == std::ios::traits_type::eof();
    return info.loaded;
}


template<typename Arch, typename Transformer>
bool Network<Arch, Transformer>::write_parameters(std::ostream&      stream,
                                                  const std::string& netDescription) const {
    if (!write_header(stream, Network::hash, netDescription))
        return false;
    if (!Detail::write_parameters(stream, featureTransformer))
        return false;
    for (std::size_t i = 0; i < LayerStacks; ++i)
    {
        if (!Detail::write_parameters(stream, network[i]))
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
