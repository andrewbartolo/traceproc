/*
 * Helper class for MNStats Nodes.
 * NOTE: there is no corresponding .cpp file; everything is declared inline and
 * defined in this .h file.
 */
#pragma once

#include <cstdint>

#include "../common/defs.h"


class Node {
    public:
        Node(node_id_t index);
        Node(const Node& n) = delete;
        Node& operator=(const Node& n) = delete;
        Node(Node&& n) = default;
        Node& operator=(Node&& n) = default;
        ~Node();

        void do_read();
        void do_write();
        uint64_t get_reads();
        uint64_t get_writes();

    private:
        node_id_t index;

        uint64_t n_reads = 0;
        uint64_t n_writes = 0;




};


/*
 * Inline class definitions.
 */
inline void
Node::do_read()
{
    ++n_reads;
}


inline void
Node::do_write()
{
    ++n_writes;
}


inline uint64_t
Node::get_reads()
{
    return n_reads;
}


inline uint64_t
Node::get_writes()
{
    return n_writes;
}
