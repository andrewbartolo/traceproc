/*
 * Takes in multiple input arguments, including
 * 1. directory containing bittrack.{txt, bin}, and
 * 2. directory containing memtrace.bin,
 * and gives progressive lifetime estimates of how long the system will last.
 */
#pragma once

#include <cstdbool>
#include <cstdint>
#include <iterator>
#include <list>
#include <random>
#include <string>
#include <unordered_map>
#include <unistd.h>
#include <vector>

#include "../common/defs.h"
#include "../common/MemTraceReader.h"


class SNQueues {
    public:
        SNQueues(int argc, char* argv[]);
        SNQueues(const SNQueues& snq) = delete;
        SNQueues& operator=(const SNQueues& snq) = delete;
        SNQueues(SNQueues&& snq) = delete;
        SNQueues& operator=(SNQueues&& snq) = delete;
        ~SNQueues();

        void run();
        void dump_stats(bool final = false);

    private:
        typedef struct __attribute__((packed)) {
            page_addr_t page_addr;
            double page_wf;
        } bittrack_entry_t;

        typedef struct frame_meta_t {
            uint64_t interval_bfs;
            uint64_t lifetime_bfs;
            // (figurative) backpointer to the queue we're in
            // (store a vector index, and not a raw pointer, so we can get the
            // "next" queue up after this when promoting)
            size_t queue;
            // (figurative) backpointer to the page_addr mapped to us
            page_addr_t page_addr;
        } frame_meta_t;

        typedef enum {
            WF_MODE_AVERAGE,
            WF_MODE_PER_PAGE,
            WF_MODE_INVALID
        } write_factor_mode_t;


        void parse_and_validate_args(int argc, char* argv[]);
        void read_bittrack_files();


        // input arguments
        uint64_t n_buckets;
        uint64_t cell_write_endurance;
        std::string memtrace_directory;
        std::string bittrack_directory;
        std::string write_factor_mode_str;
        write_factor_mode_t write_factor_mode;
        double trace_time_s;
        uint64_t n_bytes_requested;
        uint64_t n_iterations = std::numeric_limits<uint64_t>::max();

        // derived, or from input files
        uint64_t bucket_interval;
        uint64_t bucket_cap;
        uint64_t n_pages_requested;
        uint64_t n_bytes_mem;
        uint64_t n_pages_mem;
        uint64_t n_bytes_rss;
        uint64_t n_pages_rss;
        MemTraceReader mtr;
        std::unordered_map<std::string, std::string> bittrack_kv;
        std::unordered_map<page_addr_t, double> page_wfs;
        std::unordered_map<page_addr_t, uint64_t> page_bfpws;
        double average_wf;
        uint64_t average_bfpw;
        uint64_t line_size;
        uint64_t page_size;
        uint64_t line_size_log2;
        uint64_t page_size_log2;
        uint64_t bits_per_line;
        uint64_t bits_per_page;

        // internal mechanics
        std::unordered_map<page_addr_t, std::list<frame_meta_t*>::iterator>
                page_map;
        std::vector<std::list<frame_meta_t*>> queues_vec;
        double system_time_s = 0.0;

        // memoize some things to keep some operations O(1)
        frame_meta_t* most_written_frame = nullptr;
        size_t lowest_active_queue = 0;
};
