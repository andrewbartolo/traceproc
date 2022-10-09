/*
 * Tool for simulating a set-associative cache which sits between compute cores
 * and main memory.
 */
#pragma once

#include <unordered_map>
#include <vector>

#include "Cache/defs.h"
#include "Cache/Bank.h"

// forward declarations
class Bank;


class Cache {
    public:
        Cache(uint64_t n_lines, uint64_t n_banks, uint64_t n_ways,
                allocation_policy_t allocation_policy,
                eviction_policy_t eviction_policy);
        Cache(const Cache& c) = delete;
        Cache& operator=(const Cache& c) = delete;
        Cache(Cache&& c) = delete;
        Cache& operator=(Cache&& c) = delete;
        ~Cache();

        access_result_t access(line_addr_t addr, mem_ref_type_t type,
                line_addr_t& evicted_line_addr);
        access_result_t access(line_addr_t line_addr, mem_ref_type_t type);

        void aggregate_stats();
        void clear_stats();
        uint64_t get_n_rd_hits();
        uint64_t get_n_wr_hits();
        uint64_t get_n_rd_misses();
        uint64_t get_n_wr_misses();
        uint64_t get_n_evictions();

    private:
        uint32_t fast_hash(uint64_t in, uint64_t modulo);

        size_t n_lines;
        size_t n_banks;
        size_t n_ways;
        allocation_policy_t allocation_policy;
        eviction_policy_t eviction_policy;

        // simulation data structures
        std::vector<Bank> banks;

        // derived stats
        uint64_t n_rd_hits = 0;
        uint64_t n_wr_hits = 0;
        uint64_t n_rd_misses = 0;
        uint64_t n_wr_misses = 0;
        uint64_t n_evictions = 0;

        friend class Bank;
        friend class Set;
};


/*
 * Inline function definitions.
 */
inline uint32_t
Cache::fast_hash(uint64_t in, uint64_t modulo)
{
    uint32_t res = 0;
    uint64_t tmp = in;
    for (uint32_t i = 0; i < 4; ++i) {
        res ^= (uint32_t) ( ((uint64_t) 0xffff) & tmp);
        tmp = tmp >> 16;
    }
    return res % modulo;
}


inline uint64_t
Cache::get_n_rd_hits()
{
    return this->n_rd_hits;
}


inline uint64_t
Cache::get_n_wr_hits()
{
    return this->n_wr_hits;
}


inline uint64_t
Cache::get_n_rd_misses()
{
    return this->n_rd_misses;
}


inline uint64_t
Cache::get_n_wr_misses()
{
    return this->n_wr_misses;
}


inline uint64_t
Cache::get_n_evictions()
{
    return this->n_evictions;
}
