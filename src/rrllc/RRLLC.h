/*
 * Uses traces to simulate a last-level cache (LLC), along with a cache for
 * Randomized Rotation (RR) values.
 */
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

#include "Cache.h"
#include "Cache/defs.h"
#include "../common/defs.h"
#include "../common/MemTraceReader.h"


class RRLLC {
    public:
        RRLLC(int argc, char* argv[]);
        RRLLC(const RRLLC& rl) = delete;
        RRLLC& operator=(const RRLLC& rl) = delete;
        RRLLC(RRLLC&& rl) = delete;
        RRLLC& operator=(RRLLC&& rl) = delete;
        ~RRLLC();

        void run();
        void aggregate_stats();
        void dump_termination_stats();


    private:
        void parse_and_validate_args(int argc, char* argv[]);
        void run_pass();

        // input arguments
        std::string memtrace_directory;
        uint64_t line_size;
        uint64_t page_size;
        uint64_t llc_size;
        uint64_t llc_n_banks;
        uint64_t llc_n_ways;
        allocation_policy_t llc_allocation_policy;
        eviction_policy_t llc_eviction_policy;
        uint64_t rrc_n_lines;
        uint64_t rrc_n_banks;
        uint64_t rrc_n_ways;
        eviction_policy_t rrc_eviction_policy;

        // derived, or from input files
        MemTraceReader mtr;
        uint64_t lines_per_page;
        uint64_t line_size_log2;
        uint64_t page_size_log2;
        uint64_t llc_n_lines;

        // internal mechanics
        std::unique_ptr<Cache> llc;
        std::unique_ptr<Cache> rrc;

        // stats
        uint64_t llc_n_hits;
        uint64_t llc_n_accesses;
        double llc_hit_rate;
        uint64_t llc_n_evictions;

        uint64_t rrc_n_hits;
        uint64_t rrc_n_accesses;
        double rrc_hit_rate;
        uint64_t rrc_n_evictions;
};
