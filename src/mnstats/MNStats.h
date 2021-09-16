/*
 * Basic simulation for multi-chip statistics; namely,
 * 1. percentage on- vs. off-chip accesses, and
 * 2. write imbalance between multiple nodes.
 */
#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>

#include "../common/defs.h"
#include "../common/MemTraceReader.h"
#include "Node.h"
#include "Page.h"


class MNStats {
    public:
        MNStats(int argc, char* argv[]);
        MNStats(const MNStats& mns) = delete;
        MNStats& operator=(const MNStats& mns) = delete;
        MNStats(MNStats&& mns) = delete;
        MNStats& operator=(MNStats&& mns) = delete;
        ~MNStats();

        void run();
        void aggregate_stats();
        void dump_termination_stats();


    private:
        typedef enum {
            ALLOCATION_MODE_FIRST_TOUCH,
            ALLOCATION_MODE_INTERLEAVE,
            ALLOCATION_MODE_INVALID
        } allocation_mode_t;

        void parse_and_validate_args(int argc, char* argv[]);
        Page& map_addr_to_page(page_addr_t page_addr, node_id_t
                requesting_node);

        // input arguments
        std::string memtrace_directory;
        std::string allocation_mode_str;
        allocation_mode_t allocation_mode;
        node_id_t n_nodes;
        uint64_t line_size;
        uint64_t page_size;
        uint64_t line_size_log2;
        uint64_t page_size_log2;

        // derived, or from input files
        MemTraceReader mtr;

        // internal mechanics
        std::vector<Node> nodes;
        std::unordered_map<page_addr_t, Page> pages;
        node_id_t curr_interleave_node = 0;

        // stats
        uint64_t on_node_reads = 0;
        uint64_t off_node_reads = 0;
        uint64_t on_node_writes = 0;
        uint64_t off_node_writes = 0;
        uint64_t on_node_accesses = 0;
        uint64_t off_node_accesses = 0;
        double p_on_node_accesses = 0.0;
        uint64_t all_reads = 0;
        uint64_t all_writes = 0;
        uint64_t max_node_reads = 0;
        uint64_t max_node_writes = 0;
        double avg_reads_per_node = 0.0;
        double avg_writes_per_node = 0.0;
        double p_max_node_reads = 0.0;
        double p_max_node_writes = 0.0;
        double avg_node_frac = 0.0;
        double diff_p_max_vs_avg_reads = 0.0;
        double diff_p_max_vs_avg_writes = 0.0;
        std::vector<uint64_t> node_rss_pages;
};
