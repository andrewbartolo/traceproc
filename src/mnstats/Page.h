/*
 * Helper class for MNStats Pages.
 * NOTE: most functions are declared inline and defined in this .h file.
 */
#pragma once

#include <cstdint>
#include <vector>

#include "../common/defs.h"


class Page {
    public:
        Page(node_id_t placement, node_id_t n_nodes);
        Page(const Page& p) = delete;
        Page& operator=(const Page& p) = delete;
        Page(Page&& p) = default;
        Page& operator=(Page&& p) = default;
        ~Page();

        bool do_read(node_id_t requesting_node);
        bool do_write(node_id_t requesting_node);
        node_id_t get_placement();
        uint64_t get_on_node_reads();
        uint64_t get_off_node_reads();
        uint64_t get_on_node_writes();
        uint64_t get_off_node_writes();

    private:
        node_id_t placement;

        std::vector<uint64_t> node_accesses_since_placement;
        uint64_t sum_node_accesses_since_placement;


        uint64_t on_node_reads = 0;
        uint64_t off_node_reads = 0;
        uint64_t on_node_writes = 0;
        uint64_t off_node_writes = 0;
};


/*
 * Inline class definitions.
 */
/*
 * Returns true if on-node; false if off-node.
 */
inline bool
Page::do_read(node_id_t requesting_node)
{
    placement == requesting_node ? ++on_node_reads : ++off_node_reads;
    ++node_accesses_since_placement[requesting_node];
    ++sum_node_accesses_since_placement;
    return placement == requesting_node;
}


inline bool
Page::do_write(node_id_t requesting_node)
{
    placement == requesting_node ? ++on_node_writes : ++off_node_writes;
    ++node_accesses_since_placement[requesting_node];
    ++sum_node_accesses_since_placement;
    return placement == requesting_node;
}


inline node_id_t
Page::get_placement()
{
    return placement;
}


inline uint64_t
Page::get_on_node_reads()
{
    return on_node_reads;
}


inline uint64_t
Page::get_off_node_reads()
{
    return off_node_reads;
}

inline uint64_t
Page::get_on_node_writes()
{
    return on_node_writes;
}

inline uint64_t
Page::get_off_node_writes()
{
    return off_node_writes;
}
