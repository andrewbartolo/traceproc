#include <algorithm>
#include <fstream>
#include <iostream>
#include <sstream>
#include <unistd.h>

#include "../common/util.h"
#include "SNStats.h"



SNStats::SNStats(int argc, char* argv[])
{
    parse_and_validate_args(argc, argv);

    std::string memtrace_filepath = memtrace_directory + "/" + "memtrace.bin";
    mtr.load(memtrace_filepath);
}


SNStats::~SNStats()
{
}


void
SNStats::parse_and_validate_args(int argc, char* argv[])
{
    int c;
    optind = 0; // global: clear previous getopt() state, if any
    opterr = 0; // global: don't explicitly warn on unrecognized args
    int n_args_parsed = 0;

    // sentinels
    memtrace_directory = "";
    line_size = 0;
    page_size = 0;

    // parse
    while ((c = getopt(argc, argv, "m:l:p:")) != -1) {
        try {
            switch (c) {
                case 'm':
                    memtrace_directory = optarg;
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

    if (memtrace_directory == "")
        print_message_and_die("must supply MemTrace input directory (-m)");

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


    lines_per_page = page_size / line_size;

    line_size_log2 = __builtin_ctzll(line_size);
    page_size_log2 = __builtin_ctzll(page_size);
}


void
SNStats::run()
{
    while (!mtr.is_end_of_pass()) {
        auto& mt = mtr.next();
        line_addr_t line_addr = mt.line_addr;
        page_addr_t page_addr = line_addr_to_page_addr(line_addr,
                line_size_log2, page_size_log2);
        bool is_write = mt.is_write;

        if (is_write) {
            ++line_write_counts[line_addr];
            ++page_write_counts[page_addr];
        }
    }
}


void
SNStats::aggregate_stats()
{
    // find the most-written line
    auto& mwl = *std::max_element(line_write_counts.begin(),
            line_write_counts.end(), [](
            const std::pair<line_addr_t, uint64_t>& l0,
            const std::pair<line_addr_t, uint64_t>& l1) {
                return l0.second < l1.second;
            }
    );
    most_written_line_n_writes = mwl.second;

    // find the most-written page
    auto& mwp = *std::max_element(page_write_counts.begin(),
            page_write_counts.end(), [](
            const std::pair<line_addr_t, uint64_t>& l0,
            const std::pair<line_addr_t, uint64_t>& l1) {
                return l0.second < l1.second;
            }
    );
    most_written_page_n_writes = mwp.second;

    most_written_line_bytes_written = most_written_line_n_writes * line_size;
    most_written_page_bytes_written = most_written_page_n_writes * line_size;
}


void
SNStats::dump_termination_stats()
{
    // using a stringstream, dump to both file and stdout
    std::stringstream ss;

    ss << "LINE_SIZE" << " " << line_size << std::endl;
    ss << "PAGE_SIZE" << " " << page_size << std::endl;
    ss << "MOST_WRITTEN_LINE_WRITES" << " " << most_written_line_n_writes <<
            std::endl;
    ss << "MOST_WRITTEN_PAGE_WRITES" << " " << most_written_page_n_writes <<
            std::endl;
    ss << "MOST_WRITTEN_LINE_BYTES_WRITTEN" << "  " <<
            most_written_line_bytes_written << std::endl;
    ss << "MOST_WRITTEN_PAGE_BYTES_WRITTEN" << "  " <<
            most_written_page_bytes_written << std::endl;

    std::cout << ss.rdbuf()->str();

    std::ofstream ofs("snstats.txt", std::ofstream::out);
    ofs << ss.rdbuf()->str();
}


int
main(int argc, char* argv[])
{
    SNStats sns(argc, argv);

    sns.run();
    sns.aggregate_stats();
    sns.dump_termination_stats();

    return 0;
}
