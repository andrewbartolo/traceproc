/*
 * Implementation of an individual cache set, with N ways.
 *
 * NOTE: currently, we use runtime logic to bifurcate between the various
 * eviction policies, which have different implementations. In the future, it
 * may be preferable to, e.g., split into LRUSet and RandomSet to avoid some
 * runtime overhead.
 */
#include "Set.h"
#include "defs.h"


Set::Set(size_t gid, size_t n_ways, allocation_policy_t allocation_policy,
        eviction_policy_t eviction_policy) : gid(gid), n_ways(n_ways),
        allocation_policy(allocation_policy), eviction_policy(eviction_policy),
        rand_gen(gid), rand_dist(0, n_ways - 1)
{
    if (eviction_policy == EVICTION_POLICY_LRU) {
        // reserve some space in the map
        lru_map.reserve(n_ways);
    }
    else if (eviction_policy == EVICTION_POLICY_RANDOM) {
        // reserve space in the map and vector
        rand_vec.reserve(n_ways);
        rand_set.reserve(n_ways);
    }
    else { }

    n_ways_active = 0;
}

/*
 * For Blocks:
 * read and write hits: nothing, already handled in Cache::access()
 * read miss: apply_read;
 * write miss: nothing
 * eviction: apply_write()
 */
access_result_t
Set::access(line_addr_t line_addr, mem_ref_type_t type,
        line_addr_t& evicted_line_addr)
{
    // zero the passed-by-reference output parameter
    evicted_line_addr = 0x0;

    if (eviction_policy == EVICTION_POLICY_LRU) {
        // is it a hit?
        auto it = lru_map.find(line_addr);
        if (it != lru_map.end()) {
            // it was a hit
            (type == MEM_REF_TYPE_LD) ? ++n_rd_hits : ++n_wr_hits;

            // reset the last-used time by removing and appending
            lru_list.erase(it->second);
            auto new_it = lru_list.emplace(lru_list.end(), line_addr);
            lru_map[line_addr] = new_it;

            return ACCESS_RESULT_HIT | ACCESS_RESULT_NO_EVICTION;
        }
        else {
            // it was a miss
            (type == MEM_REF_TYPE_LD) ? ++n_rd_misses : ++n_wr_misses;

            // based on our allocation policy, do we want to allocate or not?
            bool do_allocate = (type == MEM_REF_TYPE_ST) or
                    ((type == MEM_REF_TYPE_LD)
                    and (allocation_policy == ALLOCATION_POLICY_AORW));

            if (do_allocate) {
                if (n_ways_active == n_ways) {
                    // need to evict least-recent element (map erase first!)
                    // don't forget to cache the value pointed to by the
                    // iterator, since we'll delete the iterator pos in the list
                    auto to_evict_list_it = lru_list.begin();
                    auto to_evict_line_addr = *to_evict_list_it;
                    lru_map.erase(to_evict_line_addr);
                    lru_list.erase(to_evict_list_it);

                    // insert the new element
                    auto new_it = lru_list.emplace(lru_list.end(), line_addr);
                    lru_map[line_addr] = new_it;

                    ++n_evictions;
                    evicted_line_addr = to_evict_line_addr;
                    return ACCESS_RESULT_MISS | ACCESS_RESULT_EVICTION;
                }
                else {
                    // don't need to evict
                    // insert the new element
                    auto new_it = lru_list.emplace(lru_list.end(), line_addr);
                    lru_map[line_addr] = new_it;

                    ++n_ways_active;
                    return ACCESS_RESULT_MISS | ACCESS_RESULT_NO_EVICTION;
                }
            }
        }
    }


    else if (eviction_policy == EVICTION_POLICY_RANDOM) {
        // is it a hit?
        auto it = rand_set.find(line_addr);
        if (it != rand_set.end()) {
            // it was a hit
            (type == MEM_REF_TYPE_LD) ? ++n_rd_hits : ++n_wr_hits;
            return ACCESS_RESULT_HIT | ACCESS_RESULT_NO_EVICTION;
        }
        else {
            // it was a miss
            (type == MEM_REF_TYPE_LD) ? ++n_rd_misses : ++n_wr_misses;

            // based on our allocation policy, do we want to allocate or not?
            bool do_allocate = (type == MEM_REF_TYPE_ST) or
                    ((type == MEM_REF_TYPE_LD)
                    and (allocation_policy == ALLOCATION_POLICY_AORW));

            if (do_allocate) {
                if (n_ways_active == n_ways) {
                    // choose a random element to evict
                    size_t vec_idx = rand_dist(rand_gen);
                    line_addr_t to_evict = rand_vec[vec_idx];
                    rand_vec[vec_idx] = line_addr;

                    rand_set.erase(to_evict);
                    rand_set.emplace(line_addr);

                    ++n_evictions;
                    evicted_line_addr = to_evict;
                    return ACCESS_RESULT_MISS | ACCESS_RESULT_EVICTION;
                }
                else {
                    // append to the vector
                    rand_vec.emplace_back(line_addr);
                    rand_set.emplace(line_addr);

                    ++n_ways_active;
                    return ACCESS_RESULT_MISS | ACCESS_RESULT_NO_EVICTION;
                }
            }
        }
    }

    // should never get here
    return ACCESS_RESULT_INVALID;
}
