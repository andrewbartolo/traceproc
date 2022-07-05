/*
 * Basic simulation for multi-chip statistics; namely,
 * 1. percentage on- vs. off-chip accesses, and
 * 2. write imbalance between multiple nodes.
 */
#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>

#include "../common/defs.h"
#include "../common/MemTraceReader.h"


class SNStats {
    public:
        SNStats(int argc, char* argv[]);
        SNStats(const SNStats& mns) = delete;
        SNStats& operator=(const SNStats& mns) = delete;
        SNStats(SNStats&& mns) = delete;
        SNStats& operator=(SNStats&& mns) = delete;
        ~SNStats();

        void run();
        void aggregate_stats();
        void dump_termination_stats();


    private:
        void parse_and_validate_args(int argc, char* argv[]);

        // input arguments
        std::string memtrace_directory;
        uint64_t line_size;
        uint64_t page_size;

        // derived, or from input files
        MemTraceReader mtr;
        uint64_t lines_per_page;
        uint64_t line_size_log2;
        uint64_t page_size_log2;

        // internal mechanics
        std::unordered_map<page_addr_t, uint64_t> page_write_counts;
        std::unordered_map<line_addr_t, uint64_t> line_write_counts;

        // stats
        uint64_t most_written_line_n_writes = 0;
        uint64_t most_written_page_n_writes = 0;
        uint64_t most_written_line_bytes_written = 0;
        uint64_t most_written_page_bytes_written = 0;
};
