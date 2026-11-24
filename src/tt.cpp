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

#include "tt.h"

#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>

#include "bitboard.h"
#include "memory.h"
#include "misc.h"
#include "syzygy/tbprobe.h"
#include "thread.h"

namespace Stockfish {


// TTEntry holds 8 bytes. Its 21-bit key is packed in the cluster's key word.
// depth       8 bit
// generation  5 bit
// pv node     1 bit
// bound type  2 bit
// move       16 bit
// value      16 bit
// evaluation 16 bit
//
// These fields are in the same order as accessed by TT::probe(), since memory
// is fastest sequentially. The store order in write() matches this order.

struct TTEntry {

    // Extract data from the TT entry. We have to convert TT internal bitfields
    // to external types.
    TTData read() const {
        return TTData{Move(move16),           Value(value16),
                      Value(eval16),          Depth(depth8 + DEPTH_ENTRY_OFFSET),
                      Bound(genBound8 & 0x3), bool(genBound8 & 0x4)};
    }

    // Check if the TT entry is occupied
    bool is_occupied() const;

    // Return TT entry age
    // The returned age is a multiple of TranspositionTable::GENERATION_DELTA
    u8 relative_age(const u8 generation8) const;

   private:
    friend class TranspositionTable;
    friend struct TTWriter;

    RelaxedAtomic<u8>   depth8;
    RelaxedAtomic<u8>   genBound8;
    RelaxedAtomic<Move> move16;
    RelaxedAtomic<i16>  value16;
    RelaxedAtomic<i16>  eval16;
};

// `genBound8` is where most of the details are. We use the following constants to manipulate 5 leading generation bits
// and 3 trailing miscellaneous bits.

// These bits are reserved for other things.
static constexpr unsigned GENERATION_BITS = 3;
// increment for generation field
static constexpr int GENERATION_DELTA = (1 << GENERATION_BITS);
// cycle length
static constexpr int GENERATION_CYCLE = 255 + GENERATION_DELTA;
// mask to pull out generation number
static constexpr int GENERATION_MASK = (0xFF << GENERATION_BITS) & 0xFF;

// DEPTH_ENTRY_OFFSET exists because 1) we use `bool(depth8)` as the occupancy check, but
// 2) we need to store negative depths for QS. (`depth8` is the only field with "spare bits":
// we sacrifice the ability to store depths greater than 1<<8 less the offset, as asserted in `write`.)
bool TTEntry::is_occupied() const { return bool(depth8); }

// Three 8-byte entries and one packed 63-bit key word fit in 32 bytes.
static constexpr int ClusterSize = 3;
static constexpr int KeyBits = 21;
static constexpr u64 KeyMask = (1ULL << KeyBits) - 1;

struct Cluster {
    RelaxedAtomic<u64> keys;
    TTEntry entry[ClusterSize];
};

static_assert(sizeof(TTEntry) == 8, "Unexpected TT entry size");
static_assert(sizeof(Cluster) == 32, "Suboptimal Cluster size");

// Populates the TTEntry with a new node's data, possibly overwriting an old
// position. The update is non-atomic and can be racy. We convert external
// types to internal bitfields.
void TTWriter::write(
  Key k, Value v, bool pv, Bound b, Depth d, Move m, Value ev, u8 generation8) {

    const int shift   = KeyBits * index;
    const u64 keys    = cluster->keys;
    const u64 keyDiff = ((keys >> shift) ^ k) & KeyMask;
    auto&     entry   = cluster->entry[index];

    // Preserve the old ttmove if we don't have a new one
    if (m || keyDiff)
        entry.move16 = m;

    // Overwrite less valuable entries (cheapest checks first)
    if (b == BOUND_EXACT || keyDiff
        || d - DEPTH_ENTRY_OFFSET + 2 * pv > entry.depth8 - 4
        || entry.relative_age(generation8))
    {
        assert(d > DEPTH_ENTRY_OFFSET);
        assert(d < 256 + DEPTH_ENTRY_OFFSET);

        cluster->keys   = keys ^ (keyDiff << shift);
        entry.depth8    = u8(d - DEPTH_ENTRY_OFFSET);
        entry.genBound8 = u8(generation8 | u8(pv) << 2 | b);
        entry.value16   = i16(v);
        entry.eval16    = i16(ev);
    }
    else if (entry.depth8 + DEPTH_ENTRY_OFFSET >= 5
             && Bound(entry.genBound8 & 0x3) != BOUND_EXACT)
        entry.depth8 = std::max(int(entry.depth8) - 1, 0);
}


u8 TTEntry::relative_age(const u8 generation8) const {
    // Due to our packed storage format for generation and its cyclic
    // nature we add GENERATION_CYCLE (256 is the modulus, plus what
    // is needed to keep the unrelated lowest n bits from affecting
    // the result) to calculate the entry age correctly even after
    // generation8 overflows into the next cycle.
    return (GENERATION_CYCLE + generation8 - genBound8) & GENERATION_MASK;
}


// TTWriter identifies the packed key and entry in its cluster.
TTWriter::TTWriter(Cluster* cl, int idx) :
    cluster(cl), index(idx) {}

// Flag a TTEntry as useless (decrementing the stored depth by the given penalty)
void TTWriter::penalize(int penalty) {
    auto& entry = cluster->entry[index];
    entry.depth8 = std::max(int(entry.depth8) - penalty, 0);
}


// Sets the size of the transposition table, measured in megabytes.
// Transposition table consists of clusters and each cluster consists
// of ClusterSize number of TTEntry.
void TranspositionTable::resize(usize mbSize, ThreadPool& threads) {
    aligned_large_pages_free(table);

    clusterCount = mbSize * 1024 * 1024 / sizeof(Cluster);

    table = static_cast<Cluster*>(aligned_large_pages_alloc(clusterCount * sizeof(Cluster)));

    if (!table)
    {
        std::cerr << "Failed to allocate " << mbSize << "MB for transposition table." << std::endl;
        exit(EXIT_FAILURE);
    }

    clear(threads);
}


// Initializes the entire transposition table to zero, in a multi-threaded way
void TranspositionTable::clear(ThreadPool& threads) {
    generation8              = 0;
    const usize threadCount = threads.num_threads();

    for (usize i = 0; i < threadCount; ++i)
    {
        threads.run_on_thread(i, [this, i, threadCount]() {
            // Each thread will zero its part of the hash table
            const usize stride = clusterCount / threadCount;
            const usize start  = stride * i;
            const usize len    = i + 1 != threadCount ? stride : clusterCount - start;

            std::memset(static_cast<void*>(&table[start]), 0, len * sizeof(Cluster));
        });
    }

    for (usize i = 0; i < threadCount; ++i)
        threads.wait_on_thread(i);
}


// Returns an approximation of the hashtable occupation during a search.
// The hash is x permill full, as per UCI protocol. Only counts entries
// which are younger than maxAge.
int TranspositionTable::hashfull(int maxAge) const {
    int maxAgeInternal = maxAge << GENERATION_BITS;
    int cnt            = 0;
    for (int i = 0; i < 1000; ++i)
        for (int j = 0; j < ClusterSize; ++j)
            cnt += table[i].entry[j].is_occupied()
                && table[i].entry[j].relative_age(generation8) <= maxAgeInternal;

    return cnt / ClusterSize;
}


// Must be called at the beginning of each root search to track entry aging
void TranspositionTable::new_search() {
    // increment by delta to keep lower bits as is
    generation8 += GENERATION_DELTA;
}


// The current age, used when writing new data to the TT
u8 TranspositionTable::generation() const { return generation8; }


// Looks up the current position in the transposition table. Calling probe(key)
// returns true if the key is found (which may be a collision) and has non-null
// data. Otherwise, it returns false and a pointer to an empty or least valuable
// TTEntry to be replaced later. The value of an entry is its depth minus 8 times
// its relative age.
std::tuple<bool, TTData, TTWriter> TranspositionTable::probe(const Key key) const {

    Cluster* const cluster = get_cluster(key);
    auto& tte = cluster->entry;
    const u64 key21 = key & KeyMask;

    constexpr u64 KeyLsb = 1ULL | (1ULL << KeyBits) | (1ULL << (2 * KeyBits));
    constexpr u64 KeyMsb = KeyLsb << (KeyBits - 1);
    const u64 keyXors = u64(cluster->keys) ^ (KeyLsb * key21);
    // Borrow propagation may mark later fields, but the first matching field is exact.
    const u64 matches = (keyXors - KeyLsb) & ~keyXors & KeyMsb;
    if (matches)
    {
        static_assert(KeyBits == 21, "Index computation below assumes KeyBits == 21");
        const int i = (int(lsb(matches)) >> 4) - 1;
        return {tte[i].is_occupied(), tte[i].read(), TTWriter(cluster, i)};
    }

    // Retain Revolution's existing age weighting in the replacement policy.
    int replace      = 0;
    int replaceValue = tte[0].depth8 - tte[0].relative_age(generation8);
    for (int i = 1; i < ClusterSize; ++i)
    {
        const int value = tte[i].depth8 - tte[i].relative_age(generation8);
        if (replaceValue > value)
        {
            replace      = i;
            replaceValue = value;
        }
    }

    return {false,
            TTData{Move::none(), VALUE_NONE, VALUE_NONE, DEPTH_ENTRY_OFFSET, BOUND_NONE, false},
            TTWriter(cluster, replace)};
}


// The hash function; its only external use is memory prefetching
Cluster* TranspositionTable::get_cluster(const Key key) const {
    return &table[mul_hi64(key, clusterCount)];
}

}  // namespace Stockfish
