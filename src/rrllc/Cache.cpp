#include <stdio.h>
#include <unistd.h>

#include <algorithm>
#include <fstream>
#include <string>

#include "Cache.h"


/*
 * Constructor.
 */
Cache::Cache(uint64_t n_lines, uint64_t n_banks, uint64_t n_ways,
        allocation_policy_t allocation_policy,
        eviction_policy_t eviction_policy) : n_lines(n_lines), n_banks(n_banks),
        n_ways(n_ways), allocation_policy(allocation_policy),
        eviction_policy(eviction_policy)

{
    size_t n_lines_per_bank = n_lines / n_banks;
    size_t n_sets_per_bank = n_lines_per_bank / n_ways;

    // construct Banks
    for (size_t i = 0; i < n_banks; ++i) {
        size_t bank_gid = i;
        // still invokes move constructor
        banks.emplace_back(bank_gid, n_sets_per_bank, n_ways, allocation_policy,
                eviction_policy);
    }
}


/*
 * Destructor.
 */
Cache::~Cache()
{
}


/*
 * Propagate the effects of the memory access through the cache.
 */
access_result_t
Cache::access(line_addr_t line_addr, mem_ref_type_t type,
        line_addr_t& evicted_line_addr)
{
    // hash to get a bank
    size_t bank_idx = fast_hash(line_addr, n_banks);
    return banks[bank_idx].access(line_addr, type, evicted_line_addr);
}


access_result_t
Cache::access(line_addr_t line_addr, mem_ref_type_t type)
{
    // just ignore evicted_line_addr 
    line_addr_t _unused;
    return this->access(line_addr, type, _unused);
}


void
Cache::aggregate_stats()
{
    // Set statistics
    for (auto& b : banks) {
        for (auto& s : b.sets) {
            n_rd_hits += s.n_rd_hits;
            n_wr_hits += s.n_wr_hits;
            n_rd_misses += s.n_rd_misses;
            n_wr_misses += s.n_wr_misses;
            n_evictions += s.n_evictions;
        }
    }
}


/*
 * Clears stats, but does not clear internal state pertaining to which lines
 * hold which data.
 */
void
Cache::clear_stats()
{
    for (auto& b : banks) {
        for (auto& s : b.sets) {
            s.n_rd_hits = 0;
            s.n_wr_hits  = 0;
            s.n_rd_misses = 0;
            s.n_wr_misses = 0;
            s.n_evictions = 0;
        }
    }

    // clear Cache's aggregated stats too
    n_rd_hits = 0;
    n_wr_hits = 0;
    n_rd_misses = 0;
    n_wr_misses = 0;
    n_evictions = 0;
}
