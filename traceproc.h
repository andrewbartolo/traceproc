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

/*
 * Inner class that tracks metadata for a given virtual page (VPage).
 */
class VPage {
    public:
        VPage(uint16_t placement);
        bool do_read(uint16_t requesting_node);
        bool do_write(uint16_t requesting_node);

        uint16_t placement;
        uint64_t on_node_reads = 0;
        uint64_t on_node_writes = 0;
        uint64_t off_node_reads = 0;
        uint64_t off_node_writes = 0;
};

/*
 * Inline class definitions.
 */
/*
 * Returns true if on-node, false if off-node.
 */
inline bool
VPage::do_read(uint16_t requesting_node)
{
    (placement == requesting_node) ? ++on_node_reads : ++off_node_reads;
    return placement == requesting_node;
}

inline bool
VPage::do_write(uint16_t requesting_node)
{
    (placement == requesting_node) ? ++on_node_writes : ++off_node_writes;
    return placement == requesting_node;
}

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
        inline VPage* map_addr_to_vpage(uint64_t page_addr,
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
        std::unordered_map<uint64_t, VPage> vpages;
        std::vector<uint64_t> physical_node_reads;
        std::vector<uint64_t> physical_node_writes;
        uint64_t on_node_reads = 0;
        uint64_t on_node_writes = 0;
        uint64_t off_node_reads = 0;
        uint64_t off_node_writes = 0;
        uint64_t on_node_combined = 0;
        uint64_t off_node_combined = 0;

        double pct_on_node_combined = 0.0;

        double mean_physical_node_reads = 0.0;
        double mean_physical_node_writes = 0.0;
        double var_physical_node_reads = 0.0;
        double var_physical_node_writes = 0.0;
        double stdev_physical_node_reads = 0.0;
        double stdev_physical_node_writes = 0.0;


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
