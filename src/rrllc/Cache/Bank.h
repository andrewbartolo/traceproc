/*
 * A Bank in the cache.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "defs.h"
#include "../Cache.h"
#include "Set.h"

// forward declarations
class Set;


class Bank {
    public:
        Bank(size_t gid, size_t n_sets, size_t n_ways,
                allocation_policy_t allocation_policy,
                eviction_policy_t eviction_policy);

        access_result_t access(line_addr_t line_addr, mem_ref_type_t type,
                line_addr_t& evicted_line_addr);

        // Buffer iterates over these in aggregate_stats()
        std::vector<Set> sets;

    private:
        size_t gid;
        size_t n_sets;
        size_t n_ways;
};
