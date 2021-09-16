/*
 * Takes in a write histogram trace and performs the offline portion of
 * page-level ENDUReR.
 * NOTE: currently assumes a 1:1 mapping of num. nodes to num. input write sets.
 */
#pragma once

#include <stdint.h>
#include <unistd.h>

#include <random>
#include <string>
#include <unordered_map>
#include <vector>


class Traceproc {
    public:
        Traceproc(int argc, char* argv[]);
        Traceproc(const Traceproc& tp) = delete;
        Traceproc& operator=(const Traceproc& tp) = delete;
        Traceproc(Traceproc&& tp) = delete;
        Traceproc& operator=(Traceproc&& tp) = delete;
        ~Traceproc();

        void run();

    private:
        typedef struct __attribute((packed))__ {
            unsigned node_num:15;
            unsigned is_write:1;
            uint64_t line_addr:64;
        } trace_entry_t;

        typedef enum {
            ALLOCATION_MODE_FIRST_TOUCH,
            ALLOCATION_MODE_INTERLEAVE,
            ALLOCATION_MODE_INVALID
        } allocation_mode_t;

        // max number of entries to buffered-read at one time from input file
        static constexpr size_t INPUT_BUF_N_ENTRIES = 1048576;

        void parse_and_validate_args(int argc, char* argv[]);
        void read_input_file();
        inline uint64_t line_addr_to_page_addr(uint64_t line_addr);
        inline uint16_t map_addr_to_node(uint64_t page_addr,
                uint16_t requesting_node);
        inline void process_entry(trace_entry_t* e);
        void aggregate_stats();
        void print_stats();

        // input arguments
        std::string input_filepath;
        std::string allocation_mode_str;
        allocation_mode_t allocation_mode;
        int64_t n_nodes;
        int64_t line_size;
        int64_t page_size;
        int64_t line_size_log2;
        int64_t page_size_log2;

        // stats
        std::unordered_map<uint64_t, uint16_t> placement_map;
        std::vector<std::vector<uint64_t>> read_counts;
        std::vector<std::vector<uint64_t>> write_counts;
        std::vector<std::vector<uint64_t>> combined_counts;
        std::vector<uint64_t> read_row_marginals;
        std::vector<uint64_t> write_row_marginals;
        std::vector<uint64_t> combined_row_marginals;
        std::vector<uint64_t> read_col_marginals;
        std::vector<uint64_t> write_col_marginals;
        std::vector<uint64_t> combined_col_marginals;
        uint64_t curr_interleave_node = 0;
};
