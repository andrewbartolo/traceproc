#include "Bank.h"


Bank::Bank(size_t gid, size_t n_sets, size_t n_ways,
        allocation_policy_t allocation_policy,
        eviction_policy_t eviction_policy) : gid(gid), n_sets(n_sets),
        n_ways(n_ways)
{
    for (size_t i = 0; i < n_sets; ++i) {
        size_t set_gid = gid * n_sets + i;
        // still invokes move constructor
        sets.emplace_back(set_gid, n_ways, allocation_policy, eviction_policy);
    }
}


access_result_t
Bank::access(line_addr_t line_addr, mem_ref_type_t type,
        line_addr_t& evicted_line_addr)
{
    // look up the correct Set within the Bank
    size_t set_idx = line_addr % n_sets;

    return sets[set_idx].access(line_addr, type, evicted_line_addr);
}
