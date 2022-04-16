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


class MNQueues {
    public:
        MNQueues(int argc, char* argv[]);
        MNQueues(const MNQueues& mnq) = delete;
        MNQueues& operator=(const MNQueues& mnq) = delete;
        MNQueues(MNQueues&& mnq) = delete;
        MNQueues& operator=(MNQueues&& mnq) = delete;
        ~MNQueues();

        void run();
        void dump_stats(bool final = false);

    private:
        typedef struct {
            job_id_t idx;
            double write_bw_bytes_s;
            uint64_t rss_bytes;
            double write_factor;
            uint64_t bit_writes_per_quanta;
        } job_t;

        typedef struct node_meta_t {
            uint64_t interval_bfs;
            uint64_t lifetime_bfs;
            // (figurative) backpointer to the queue we're in
            // (store a vector index, and not a raw pointer, so we can get the
            // "next" queue up after this when promoting)
            uint64_t queue;
            // (figurative) backpointer to the job currently mapped to us
            job_id_t job_idx;
        } node_meta_t;


        void parse_and_validate_args(int argc, char* argv[]);
        std::vector<MNQueues::job_t>
                parse_jobs_str(const std::string& jobs_str);
        void run_rebalance();
        void run_no_rebalance();


        // input arguments
        uint64_t n_buckets;
        uint64_t cell_write_endurance;
        uint64_t line_size;
        uint64_t page_size;
        uint64_t n_iterations = std::numeric_limits<uint64_t>::max();
        uint64_t n_bytes_mem_per_node;
        double scheduler_quanta_s;
        int rebalance;
        std::string jobs_str;

        // derived, or from input files
        uint64_t n_nodes;
        std::vector<job_t> jobs;
        uint64_t bucket_interval;
        uint64_t bucket_cap;
        uint64_t n_pages_mem_per_node;
        uint64_t line_size_log2;
        uint64_t page_size_log2;
        uint64_t bits_per_line;
        uint64_t bits_per_page;
        uint64_t bits_per_node;

        // internal mechanics
        // job_map is actually a vector, since jobs are dense and indexable
        std::vector<std::list<node_meta_t*>::iterator> job_map;
        std::vector<std::list<node_meta_t*>> queues_vec;
        uint64_t epoch = 0;
        uint64_t total_n_promotions = 0;
        uint64_t total_bytes_transferred = 0;
        uint64_t total_bytes_delay = 0;
        double system_time_s = 0.0;

        // memoize some things to keep some operations O(1)
        node_meta_t* most_written_node = nullptr;
        size_t lowest_active_queue = 0;
        // streamline the printing of stats even when no_rebalance is used
        node_meta_t no_rebalance_mwn;
};
