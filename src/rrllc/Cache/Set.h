/*
 * N-way associative Cache set.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#include <list>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <random>
#include <utility>

#include "defs.h"
#include "../Cache.h"


class Set {
    public:
        Set(size_t gid, size_t n_ways, allocation_policy_t allocation_policy,
                eviction_policy_t eviction_policy);

        access_result_t access(line_addr_t line_addr, mem_ref_type_t type,
                line_addr_t& evicted_line_addr);

        // statistics
        uint64_t n_rd_hits = 0;
        uint64_t n_wr_hits = 0;
        uint64_t n_rd_misses = 0;
        uint64_t n_wr_misses = 0;
        uint64_t n_evictions = 0;

    private:
        size_t gid;
        size_t n_ways;
        allocation_policy_t allocation_policy;
        eviction_policy_t eviction_policy;

        // mechanics
        std::list<line_addr_t> lru_list;
        std::unordered_map<line_addr_t, std::list<line_addr_t>::iterator>
                lru_map;

        std::vector<line_addr_t> rand_vec;
        std::unordered_set<line_addr_t> rand_set;
        std::mt19937 rand_gen;
        std::uniform_int_distribution<size_t> rand_dist;

        size_t n_ways_active = 0;
};
