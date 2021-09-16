#include <stdio.h>

#include <algorithm>
#include <cassert>
#include <fstream>

#include "util.h"
#include "traceproc.h"


Traceproc::Traceproc(int argc, char* argv[])
{
    parse_and_validate_args(argc, argv);

    // allocate space in the vectors
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
    n_nodes = -1;
    line_size = -1;
    page_size = -1;

    // parse
    while ((c = getopt(argc, argv, "i:m:n:l:p:")) != -1) {
        try {
            switch (c) {
                case 'i':
                    input_filepath = optarg;
                    break;
                case 'm':
                    allocation_mode_str = optarg;
                    std::transform(allocation_mode_str.begin(),
                            allocation_mode_str.end(),
                            allocation_mode_str.begin(), ::tolower);
                    if (allocation_mode_str == "firsttouch")
                        allocation_mode = ALLOCATION_MODE_FIRST_TOUCH;
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
        print_message_and_die("allocation mode must be either 'firsttouch' or "
                "'interleave': <-m MODE>");
    if (input_filepath == "")
        print_message_and_die("must supply input file: <-i INPUT_FILE>");
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

inline uint16_t
Traceproc::map_addr_to_node(uint64_t page_addr, uint16_t requesting_node)
{
    auto it = placement_map.find(page_addr);
    // return existing placement, if it exists
    if (it != placement_map.end()) return it->second;


    // place for the first time
    if (allocation_mode == ALLOCATION_MODE_FIRST_TOUCH) {
        placement_map[page_addr] = requesting_node;
    }
    else if (allocation_mode == ALLOCATION_MODE_INTERLEAVE) {
        placement_map[page_addr] = curr_interleave_node;
        curr_interleave_node = (curr_interleave_node + 1) % n_nodes;
    }

    return placement_map[page_addr];
}


inline void
Traceproc::process_entry(trace_entry_t* e)
{
    uint64_t page_addr = line_addr_to_page_addr(e->line_addr);
    bool is_write = e->is_write;
    uint16_t requesting_node = e->node_num;

    uint16_t resident_node = map_addr_to_node(page_addr, requesting_node);

    if (!is_write) ++read_counts[requesting_node][resident_node];
    else           ++write_counts[requesting_node][resident_node];
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
}

void
Traceproc::print_stats()
{
    printf("Allocation mode: %s\n", allocation_mode_str.c_str());

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
}


int
main(int argc, char* argv[])
{
    Traceproc tp(argc, argv);

    tp.run();

    return 0;
}
