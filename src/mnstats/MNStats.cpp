#include <algorithm>
#include <fstream>
#include <iostream>
#include <sstream>
#include <unistd.h>

#include "../common/util.h"
#include "MNStats.h"



MNStats::MNStats(int argc, char* argv[])
{
    parse_and_validate_args(argc, argv);

    for (node_id_t i = 0; i < n_nodes; ++i)
        nodes.emplace_back(i);

    node_rss_pages.resize(n_nodes);

    std::string memtrace_filepath = memtrace_directory + "/" + "memtrace.bin";
    mtr.load(memtrace_filepath);

}


MNStats::~MNStats()
{
}


void
MNStats::parse_and_validate_args(int argc, char* argv[])
{
    int c;
    optind = 0; // global: clear previous getopt() state, if any
    opterr = 0; // global: don't explicitly warn on unrecognized args
    int n_args_parsed = 0;

    // sentinels
    allocation_mode_str = "";
    allocation_mode = ALLOCATION_MODE_INVALID;
    memtrace_directory = "";
    n_nodes = 0;
    line_size = 0;
    page_size = 0;

    // parse
    while ((c = getopt(argc, argv, "a:m:n:l:p:")) != -1) {
        try {
            switch (c) {
                case 'a':
                    allocation_mode_str = optarg;
                    std::transform(allocation_mode_str.begin(),
                            allocation_mode_str.end(),
                            allocation_mode_str.begin(), ::tolower);

                    // firsttouch, first_touch, first, ft, etc. all match
                    if (allocation_mode_str.find("first") !=
                            std::string::npos) {
                        allocation_mode = ALLOCATION_MODE_FIRST_TOUCH;
                    }
                    if (allocation_mode_str.find("ft") !=
                            std::string::npos) {
                        allocation_mode = ALLOCATION_MODE_FIRST_TOUCH;
                    }

                    // int, interleave, etc. all match
                    if (allocation_mode_str.find("int") !=
                            std::string::npos) {
                        allocation_mode = ALLOCATION_MODE_INTERLEAVE;
                    }
                    break;
                case 'm':
                    memtrace_directory = optarg;
                    break;
                case 'n':
                    n_nodes = shorthand_to_integer(optarg, 1000);
                    break;
                case 'l':
                    line_size = shorthand_to_integer(optarg, 1024);
                    break;
                case 'p':
                    page_size = shorthand_to_integer(optarg, 1024);
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
        print_message_and_die("each argument must be accompanied by a flag");

    if (allocation_mode == ALLOCATION_MODE_INVALID)
        print_message_and_die("must supply allocation mode: (-a <firsttouch|"
                "interleave>)");

    if (memtrace_directory == "")
        print_message_and_die("must supply MemTrace input directory (-m)");

    if (n_nodes == 0)
        print_message_and_die("must supply n. nodes (-n)");

    if (line_size == 0)
        print_message_and_die("must supply line size (-l)");

    if (page_size == 0)
        print_message_and_die("must supply page size (-p)");

    if (line_size > page_size)
        print_message_and_die("line size (-l) must be <= page size (-p)");

    if (__builtin_popcountll(line_size) != 1)
        print_message_and_die("line size (-l) must be a power of 2");

    if (__builtin_popcountll(page_size) != 1)
        print_message_and_die("page size (-p) must be a power of 2");


    line_size_log2 = __builtin_ctzll(line_size);
    page_size_log2 = __builtin_ctzll(page_size);
}


void
MNStats::run()
{
    while (!mtr.is_end_of_pass()) {
        auto& mt = mtr.next();
        page_addr_t page_addr = line_addr_to_page_addr(mt.line_addr,
                line_size_log2, page_size_log2);
        node_id_t requesting_node = mt.node_num;
        bool is_write = mt.is_write;

        Page& p = map_addr_to_page(page_addr, requesting_node);

        bool is_on_node;
        if (is_write) is_on_node = p.do_write(requesting_node);
        else          is_on_node = p.do_read(requesting_node);

        if (is_write) nodes[p.get_placement()].do_write();
        else          nodes[p.get_placement()].do_read();
    }
}


Page&
MNStats::map_addr_to_page(page_addr_t page_addr, node_id_t requesting_node)
{
    auto it = pages.find(page_addr);
    // return existing placement, if it exists
    if (it != pages.end()) return it->second;


    // place for the first time
    if (allocation_mode == ALLOCATION_MODE_FIRST_TOUCH) {
        auto&& p = Page(requesting_node, n_nodes);
        pages.emplace(page_addr, std::move(p));
    }
    else if (allocation_mode == ALLOCATION_MODE_INTERLEAVE) {
        auto&& p = Page(curr_interleave_node, n_nodes);
        pages.emplace(page_addr, std::move(p));
        curr_interleave_node = (curr_interleave_node + 1) % n_nodes;
    }

    return pages.at(page_addr);
}


void
MNStats::aggregate_stats()
{
    for (auto& kv : pages) {
        auto& p = kv.second;
        on_node_reads += p.get_on_node_reads();
        off_node_reads += p.get_off_node_reads();
        on_node_writes += p.get_on_node_writes();
        off_node_writes += p.get_off_node_writes();

        node_rss_pages[p.get_placement()] += 1;
    }

    for (auto& n : nodes) {
        max_node_reads = std::max(n.get_reads(), max_node_reads);
        max_node_writes = std::max(n.get_writes(), max_node_writes);
    }

    on_node_accesses = on_node_reads + on_node_writes;
    off_node_accesses = off_node_reads + off_node_writes;

    all_reads = on_node_reads + off_node_reads;
    all_writes = on_node_writes + off_node_writes;

    p_on_node_accesses = (double) on_node_accesses /
            (double) (on_node_accesses + off_node_accesses);

    avg_reads_per_node = (double) all_reads / (double) n_nodes;
    avg_writes_per_node = (double) all_writes / (double) n_nodes;

    p_max_node_reads = (double) max_node_reads / (double) all_reads;
    p_max_node_writes = (double) max_node_writes / (double) all_writes;

    avg_node_frac = (double) 1 / (double) n_nodes;

    diff_p_max_vs_avg_reads = p_max_node_reads - avg_node_frac;
    diff_p_max_vs_avg_writes = p_max_node_writes - avg_node_frac;
}


void
MNStats::dump_termination_stats()
{
    // using a stringstream, dump to both file and stdout
    std::stringstream ss;

    ss << "ALLOCATION_MODE" << " " << allocation_mode_str << std::endl;
    ss << "NODES" << " " << n_nodes << std::endl;
    ss << "ON_NODE_READS" << " " << on_node_reads << std::endl;
    ss << "OFF_NODE_READS" << " " << off_node_reads << std::endl;
    ss << "ON_NODE_WRITES" << " " << on_node_writes << std::endl;
    ss << "OFF_NODE_WRITES" << " " << off_node_writes << std::endl;
    ss << "P_ON_NODE_ACCESSES" << " " << p_on_node_accesses << std::endl;
    ss << "AVG_READS_PER_NODE" << " " << avg_reads_per_node << std::endl;
    ss << "AVG_WRITES_PER_NODE" << " " << avg_writes_per_node << std::endl;
    ss << "DIFF_P_MAX_VS_AVG_READS" << " " << diff_p_max_vs_avg_reads <<
            std::endl;
    ss << "DIFF_P_MAX_VS_AVG_WRITES" << " " << diff_p_max_vs_avg_writes <<
            std::endl;

    // print out some per-node stats
    // (rss; lines and bytes read and written)
    for (node_id_t i = 0; i < n_nodes; ++i) {
        uint64_t p = node_rss_pages[i];
        ss << "NODE_" << i << "_RSS_PAGES" << " " << p << std::endl;
    }
    for (node_id_t i = 0; i < n_nodes; ++i) {
        uint64_t pb = node_rss_pages[i] * page_size;
        ss << "NODE_" << i << "_RSS_BYTES" << " " << pb << std::endl;
    }
    for (node_id_t i = 0; i < n_nodes; ++i) {
        uint64_t n_reads = nodes[i].get_reads();
        ss << "NODE_" << i << "_LINES_READ" << " " << n_reads << std::endl;
    }
    for (node_id_t i = 0; i < n_nodes; ++i) {
        uint64_t bytes_read = nodes[i].get_reads() * line_size;
        ss << "NODE_" << i << "_BYTES_READ" << " " << bytes_read << std::endl;
    }
    for (node_id_t i = 0; i < n_nodes; ++i) {
        uint64_t n_writes = nodes[i].get_writes();
        ss << "NODE_" << i << "_LINES_WRITTEN" << " " << n_writes << std::endl;
    }
    for (node_id_t i = 0; i < n_nodes; ++i) {
        uint64_t bytes_written = nodes[i].get_writes() * line_size;
        ss << "NODE_" << i << "_BYTES_WRITTEN" << " " << bytes_written <<
                std::endl;
    }


    std::cout << ss.rdbuf()->str();

    std::ofstream ofs("mnstats.txt", std::ofstream::out);
    ofs << ss.rdbuf()->str();
}


int
main(int argc, char* argv[])
{
    MNStats mns(argc, argv);

    mns.run();
    mns.aggregate_stats();
    mns.dump_termination_stats();

    return 0;
}
