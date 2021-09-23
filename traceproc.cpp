#include <stdio.h>

#include <algorithm>
#include <cassert>
#include <fstream>

#include "util.h"
#include "traceproc.h"


VPage::VPage(uint16_t placement, uint16_t n_nodes) : placement(placement)
{
    node_accesses_since_placement.resize(n_nodes);
}

Traceproc::Traceproc(int argc, char* argv[])
{
    parse_and_validate_args(argc, argv);

    // allocate space in the vectors
    physical_node_reads.resize(n_nodes);
    physical_node_writes.resize(n_nodes);
    read_counts.resize(n_nodes);
    write_counts.resize(n_nodes);
    combined_counts.resize(n_nodes);
    for (size_t i = 0; i < n_nodes; ++i) {
        read_counts[i].resize(n_nodes);
        write_counts[i].resize(n_nodes);
        combined_counts[i].resize(n_nodes);
    }
    read_row_marginals.resize(n_nodes);
    write_row_marginals.resize(n_nodes);
    combined_row_marginals.resize(n_nodes);
    read_col_marginals.resize(n_nodes);
    write_col_marginals.resize(n_nodes);
    combined_col_marginals.resize(n_nodes);
}

Traceproc::~Traceproc()
{

}

void
Traceproc::parse_and_validate_args(int argc, char* argv[])
{
    int c;
    optind = 0; // global: clear previous getopt() state, if any
    opterr = 0; // global: don't explicitly warn on unrecognized args
    int n_args_parsed = 0;

    // sentinels
    input_filepath = "";
    allocation_mode = ALLOCATION_MODE_INVALID;
    access_interval = -1;
    n_nodes = -1;
    line_size = -1;
    page_size = -1;

    // parse
    while ((c = getopt(argc, argv, "a:i:m:n:l:p:")) != -1) {
        try {
            switch (c) {
                case 'a':
                    access_interval = std::stol(optarg);
                    break;
                case 'i':
                    input_filepath = optarg;
                    break;
                case 'm':
                    allocation_mode_str = optarg;
                    std::transform(allocation_mode_str.begin(),
                            allocation_mode_str.end(),
                            allocation_mode_str.begin(), ::tolower);
                    if (allocation_mode_str == "ft")
                        allocation_mode = ALLOCATION_MODE_FIRST_TOUCH;
                    else if (allocation_mode_str == "ftm")
                        allocation_mode = ALLOCATION_MODE_FIRST_TOUCH_M;
                    else if (allocation_mode_str == "ftmw")
                        allocation_mode = ALLOCATION_MODE_FIRST_TOUCH_M_W;
                    else if (allocation_mode_str == "interleave")
                        allocation_mode = ALLOCATION_MODE_INTERLEAVE;
                    break;
                case 'n':
                    n_nodes = std::stol(optarg);
                    break;
                case 'l':
                    line_size = std::stol(optarg);
                    break;
                case 'p':
                    page_size = std::stol(optarg);
                    break;
                case '?':
                    print_message_and_die("unrecognized argument");
            }
        }
        catch (...) {
            print_message_and_die("generic arg parse failure");
        }
        ++n_args_parsed;
    }

    // and validate
    // the executable itself (1) plus each arg matched w/its preceding flag (*2)
    int argc_expected = 1 + (2 * n_args_parsed);
    if (argc != argc_expected)
            print_message_and_die("each argument must be accompanied by a "
            "flag");

    if (allocation_mode == ALLOCATION_MODE_INVALID)
        print_message_and_die("allocation mode must be either 'ft', 'ftm',"
                "'ftmw', or 'interleave': <-m MODE>");
    if (input_filepath == "")
        print_message_and_die("must supply input file: <-i INPUT_FILE>");
    if (access_interval == -1 and
            ((allocation_mode == ALLOCATION_MODE_FIRST_TOUCH_M) or
             (allocation_mode == ALLOCATION_MODE_FIRST_TOUCH_M_W)))
        print_message_and_die("must supply access interval: <-a INTERVAL>");
    if (n_nodes == -1)
        print_message_and_die("must supply number of nodes: <-n N_NODES>");
    if (line_size == -1)
        print_message_and_die("must supply input line size: <-l LINE_SIZE>");
    if (page_size == -1)
        print_message_and_die("must supply page size: <-p PAGE_SIZE>");
    if (__builtin_popcount(line_size) != 1)
        print_message_and_die("line size must be a power of two: "
                "<-l LINE_SIZE>");
    if (__builtin_popcount(page_size) != 1)
        print_message_and_die("page size must be a power of two: "
                "<-p PAGE_SIZE>");
    if (page_size < line_size)
        print_message_and_die("page size (-p) must be >= line size (-l)");

    // log2s
    line_size_log2 = ((8 * sizeof(line_size)) - 1) -
            __builtin_clzl(line_size);
    page_size_log2 = ((8 * sizeof(page_size)) - 1) -
            __builtin_clzl(page_size);
}

void
Traceproc::run()
{
    read_input_file();

    aggregate_stats();
    print_stats();
}

inline uint64_t
Traceproc::line_addr_to_page_addr(uint64_t line_addr)
{
    return line_addr >> (page_size_log2 - line_size_log2);
}

/*
 * Find the metadata entry in the page map, creating one if it doesn't exist.
 */
inline VPage*
Traceproc::map_addr_to_vpage(uint64_t page_addr, uint16_t requesting_node)
{
    auto it = vpages.find(page_addr);
    // return existing placement, if it exists
    if (it != vpages.end()) return &it->second;


    // place for the first time
    if ((allocation_mode == ALLOCATION_MODE_FIRST_TOUCH) or
        (allocation_mode == ALLOCATION_MODE_FIRST_TOUCH_M) or
        (allocation_mode == ALLOCATION_MODE_FIRST_TOUCH_M_W)) {
        auto vp = VPage(requesting_node, n_nodes);
        vpages.emplace(page_addr, std::move(vp));
    }
    else if (allocation_mode == ALLOCATION_MODE_INTERLEAVE) {
        auto vp = VPage(curr_interleave_node, n_nodes);
        vpages.emplace(page_addr, std::move(vp));
        curr_interleave_node = (curr_interleave_node + 1) % n_nodes;
    }

    return &vpages.at(page_addr);
}

/*
 * Migrates the VPage to its new node and resets some access-tracking state.
 */
inline void
Traceproc::do_migrate(VPage* vp, uint16_t new_node)
{
    vp->placement = new_node;

    std::fill(vp->node_accesses_since_placement.begin(),
            vp->node_accesses_since_placement.end(), 0);
    vp->sum_node_accesses_since_placement = 0;
}


inline void
Traceproc::process_entry(trace_entry_t* e)
{
    uint64_t page_addr = line_addr_to_page_addr(e->line_addr);
    bool is_write = e->is_write;
    uint16_t requesting_node = e->node_num;

    VPage* vp = map_addr_to_vpage(page_addr, requesting_node);

    bool is_on_node;
    if (is_write) is_on_node = vp->do_write(requesting_node);
    else          is_on_node = vp->do_read(requesting_node);

    if (is_write) ++physical_node_writes[vp->placement];
    else          ++physical_node_reads[vp->placement];


    // are we in a migration mode?
    if ((allocation_mode == ALLOCATION_MODE_FIRST_TOUCH) or
        (allocation_mode == ALLOCATION_MODE_INTERLEAVE)) return;

    // we're in an interval-migration mode, and we hit the interval
    if (((vp->sum_node_accesses_since_placement + 1) % access_interval) == 0) {


        // first-touch-m: migrate if accessed more often by a diff. node
        // get argmax of vector
        int64_t max_val = -1;
        uint64_t argmax;
        for (size_t i = 0; i < vp->node_accesses_since_placement.size(); ++i) {
            uint64_t a = vp->node_accesses_since_placement[i];
            if (max_val < a) {
                max_val = a;
                argmax = i;
            }
        }

        bool should_migrate = argmax != vp->placement;
        // if FIRST_TOUCH_M_W, have the add'tl constraint that destination node
        // must have fewer writes than curr. node
        if (allocation_mode == ALLOCATION_MODE_FIRST_TOUCH_M_W) {
            should_migrate &= (physical_node_writes[argmax] <
                    physical_node_writes[vp->placement]);
        }

        if (should_migrate) do_migrate(vp, argmax);
        // TODO: ++physical_node_writes for migrated-to host
    }
}

void
Traceproc::read_input_file()
{
    std::ifstream ifs(input_filepath, std::ios::binary);
    if (!ifs.is_open()) print_message_and_die("could not open input file");

    size_t input_file_size;

    // get the file size
    ifs.seekg(0, std::ios::end);
    input_file_size = ifs.tellg();
    ifs.seekg(0, std::ios::beg);

    // TODO
    line_addr_to_page_addr(0);

    if (input_file_size % sizeof(trace_entry_t) != 0)
            print_message_and_die("malformed input file; its size should be"
            " a multiple of %zu", sizeof(trace_entry_t));

    size_t n_entries = input_file_size / sizeof(trace_entry_t);

    printf("found %zu trace entries\n", n_entries);

    size_t buf_cap_n_bytes = INPUT_BUF_N_ENTRIES * sizeof(trace_entry_t);
    trace_entry_t* buf = (trace_entry_t*) malloc(buf_cap_n_bytes);

    while (!ifs.eof()) {
        // buffered read()s, since iostream::read() won't be inlined
        ifs.read((char*) buf, buf_cap_n_bytes);
        size_t n_entries_read = ifs.gcount() / sizeof(trace_entry_t);

        for (size_t i = 0; i < n_entries_read; ++i) {
            process_entry(&buf[i]);
        }
    }

    free(buf);
}

void
Traceproc::aggregate_stats()
{
    printf("Aggregating stats...\n");

    for (auto& kv : vpages) {
        auto& vp = kv.second;
        on_node_reads += vp.on_node_reads;
        on_node_writes += vp.on_node_writes;
        off_node_reads += vp.off_node_reads;
        off_node_writes += vp.off_node_writes;
    }

    on_node_combined = on_node_reads + on_node_writes;
    off_node_combined = off_node_reads + off_node_writes;

    mean_physical_node_reads = (double) std::accumulate(
            physical_node_reads.begin(), physical_node_reads.end(), 0.0) /
            (double) n_nodes;
    mean_physical_node_writes = (double) std::accumulate(
            physical_node_writes.begin(), physical_node_writes.end(), 0.0) /
            (double) n_nodes;

    std::vector<double> squared_err_reads;
    std::vector<double> squared_err_writes;
    squared_err_reads.resize(n_nodes);
    squared_err_writes.resize(n_nodes);
    for (size_t i = 0; i < n_nodes; ++i) {
        squared_err_reads[i] = std::pow((double) physical_node_reads[i] -
                mean_physical_node_reads, 2);
        squared_err_writes[i] = std::pow((double) physical_node_writes[i] -
                mean_physical_node_writes, 2);
    }

    var_physical_node_reads = (double) std::accumulate(
            squared_err_reads.begin(), squared_err_reads.end(), 0.0) /
            (double) n_nodes;
    var_physical_node_writes = (double) std::accumulate(
            squared_err_writes.begin(), squared_err_writes.end(), 0.0) /
            (double) n_nodes;
    stdev_physical_node_reads = std::sqrt(var_physical_node_reads);
    stdev_physical_node_writes = std::sqrt(var_physical_node_writes);

    max_physical_node_reads = *std::max_element(physical_node_reads.begin(),
            physical_node_reads.end());
    max_physical_node_writes = *std::max_element(physical_node_writes.begin(),
            physical_node_writes.end());

    // custom statistic: compute (max - mean) / mean
    dist_physical_node_reads = ((double) max_physical_node_reads -
            mean_physical_node_reads) / mean_physical_node_reads;
    dist_physical_node_writes = ((double) max_physical_node_writes -
            mean_physical_node_writes) / mean_physical_node_writes;


    pct_on_node_combined = (double) on_node_combined / (double)
            (on_node_combined + off_node_combined);

    /*
    for (size_t i = 0; i < n_nodes; ++i) {
        for (size_t j = 0; j < n_nodes; ++j) {
            combined_counts[i][j] = read_counts[i][j] + write_counts[i][j];

            read_row_marginals[i] += read_counts[i][j];
            write_row_marginals[i] += write_counts[i][j];
            combined_row_marginals[i] += combined_counts[i][j];

            read_col_marginals[j] += read_counts[i][j];
            write_col_marginals[j] += write_counts[i][j];
            combined_col_marginals[j] += combined_counts[i][j];
        }
    }
    */
}

void
Traceproc::print_stats()
{
    printf("Printing stats...\n");
    printf("Allocation mode: %s\n", allocation_mode_str.c_str());

    printf("Physical node reads:\n");
    for (auto& r : physical_node_reads) printf("%9zu", r);
    printf("\n");

    printf("Physical node writes:\n");
    for (auto& r : physical_node_writes) printf("%9zu", r);
    printf("\n");

    printf("Total on-node reads: %zu\n", on_node_reads);
    printf("Total off-node reads: %zu\n", off_node_reads);
    printf("Total on-node writes: %zu\n", on_node_writes);
    printf("Total off-node writes: %zu\n", off_node_writes);
    printf("Total on-node combined: %zu\n", on_node_combined);
    printf("Total off-node combined: %zu\n", off_node_combined);

    printf("Stdev, physical node reads: %.3f\n", stdev_physical_node_reads);
    printf("Stdev, physical node writes: %.3f\n", stdev_physical_node_writes);

    printf("Dist. physical node reads: %.3f\n", dist_physical_node_reads);
    printf("Dist. physical node writes: %.3f\n", dist_physical_node_writes);

    printf("Pct. on-node, combined r+w: %.3f\n", pct_on_node_combined);


    /*

    // print out the read, write, and combined matrices
    printf("Reads matrix:\n");
    for (size_t i = 0; i < n_nodes; ++i) {
        for (size_t j = 0; j < n_nodes; ++j) {
            printf("%9zu ", read_counts[i][j]);
        }
        printf("|%9zu\n", read_row_marginals[i]);
    }
    for (size_t j = 0; j < 80; ++j) printf("-");
    printf("\n");
    for (size_t j = 0; j < n_nodes; ++j) {
        printf("%9zu ", read_col_marginals[j]);
    }
    printf("\n\n\n");

    printf("Writes matrix:\n");
    for (size_t i = 0; i < n_nodes; ++i) {
        for (size_t j = 0; j < n_nodes; ++j) {
            printf("%9zu ", write_counts[i][j]);
        }
        printf("|%9zu\n", write_row_marginals[i]);
    }
    for (size_t j = 0; j < 80; ++j) printf("-");
    printf("\n");
    for (size_t j = 0; j < n_nodes; ++j) {
        printf("%9zu ", write_col_marginals[j]);
    }
    printf("\n\n\n");


    printf("Combined matrix:\n");
    for (size_t i = 0; i < n_nodes; ++i) {
        for (size_t j = 0; j < n_nodes; ++j) {
            printf("%9zu ", combined_counts[i][j]);
        }
        printf("|%9zu\n", combined_row_marginals[i]);
    }
    for (size_t j = 0; j < 80; ++j) printf("-");
    printf("\n");
    for (size_t j = 0; j < n_nodes; ++j) {
        printf("%9zu ", combined_col_marginals[j]);
    }
    printf("\n\n\n");
    */
}


int
main(int argc, char* argv[])
{
    Traceproc tp(argc, argv);

    tp.run();

    return 0;
}
