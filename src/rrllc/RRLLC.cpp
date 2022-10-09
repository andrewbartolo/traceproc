#include <algorithm>
#include <fstream>
#include <iostream>
#include <sstream>
#include <unistd.h>

#include "../common/util.h"
#include "RRLLC.h"



RRLLC::RRLLC(int argc, char* argv[])
{
    parse_and_validate_args(argc, argv);

    std::string memtrace_filepath = memtrace_directory + "/" + "memtrace.bin";
    mtr.load(memtrace_filepath);

    // initialize the LLC and RRC from input parameters
    llc = std::make_unique<Cache>(llc_n_lines, llc_n_banks, llc_n_ways,
            llc_allocation_policy, llc_eviction_policy);
    // RRC allocation policy is always AORW
    rrc = std::make_unique<Cache>(rrc_n_lines, rrc_n_banks, rrc_n_ways,
            ALLOCATION_POLICY_AORW, rrc_eviction_policy);

}


RRLLC::~RRLLC()
{
}


void
RRLLC::parse_and_validate_args(int argc, char* argv[])
{
    int c;
    optind = 0; // global: clear previous getopt() state, if any
    opterr = 0; // global: don't explicitly warn on unrecognized args
    int n_args_parsed = 0;

    // sentinels
    memtrace_directory = "";
    line_size = 0;
    page_size = 0;
    llc_size = 0;
    llc_n_banks = 0;
    llc_n_ways = 0;
    llc_allocation_policy = ALLOCATION_POLICY_INVALID;
    llc_eviction_policy = EVICTION_POLICY_INVALID;
    rrc_n_lines = 0;
    rrc_n_banks = 0;
    rrc_n_ways = 0;
    rrc_eviction_policy = EVICTION_POLICY_INVALID;

    // -m: memtrace directory
    // -l: line size in bytes
    // -p: page size in bytes
    // -s: LLC size in bytes
    // -b: LLC n. banks
    // -w: LLC n. ways
    // -a: LLC allocation policy
    // -e: LLC eviction policy
    // -r: RRC n. lines
    // -h: RRC n. banks
    // -k: RRC n. ways
    // -x: RRC eviction policy

    // parse
    while ((c = getopt(argc, argv, "m:l:p:s:b:w:a:e:r:h:k:x:")) != -1) {
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
                case 's':
                    llc_size = shorthand_to_integer(optarg, 1024);
                    break;
                case 'b':
                    llc_n_banks = std::stoul(optarg);
                    break;
                case 'w':
                    llc_n_ways = std::stoul(optarg);
                    break;
                case 'a': {
                    std::string str = optarg;
                    std::transform(str.begin(), str.end(), str.begin(),
                            ::tolower);
                    if (str == "aorw")
                            llc_allocation_policy = ALLOCATION_POLICY_AORW;
                    else if (str == "aowo")
                            llc_allocation_policy = ALLOCATION_POLICY_AOWO;
                    break;
                }
                case 'e': {
                    std::string str = optarg;
                    std::transform(str.begin(), str.end(), str.begin(),
                            ::tolower);
                    if (str == "lru")
                            llc_eviction_policy = EVICTION_POLICY_LRU;
                    else if (str == "random")
                            llc_eviction_policy = EVICTION_POLICY_RANDOM;
                    break;
                }
                case 'r':
                    rrc_n_lines = shorthand_to_integer(optarg, 1024);
                    break;
                case 'h':
                    rrc_n_banks = std::stoul(optarg);
                    break;
                case 'k':
                    rrc_n_ways = std::stoul(optarg);
                    break;
                case 'x': {
                    std::string str = optarg;
                    std::transform(str.begin(), str.end(), str.begin(),
                            ::tolower);
                    if (str == "lru")
                            rrc_eviction_policy = EVICTION_POLICY_LRU;
                    else if (str == "random")
                            rrc_eviction_policy = EVICTION_POLICY_RANDOM;
                    break;
                }
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

    if (__builtin_popcountll(line_size) != 1)
        print_message_and_die("line size (-l) must be a power of 2");

    if (page_size == 0)
        print_message_and_die("must supply page size (-p)");

    if (__builtin_popcountll(page_size) != 1)
        print_message_and_die("page size (-p) must be a power of 2");

    if (line_size > page_size)
        print_message_and_die("line size (-l) must be <= page size (-p)");

    if (llc_size == 0)
        print_message_and_die("must supply LLC size (-s)");

    if (__builtin_popcountll(llc_size) != 1)
        print_message_and_die("LLC size (-s) must be a power of 2");

    if (llc_n_banks == 0)
        print_message_and_die("must supply LLC n. banks (-b)");

    if (__builtin_popcountll(llc_n_banks) != 1)
        print_message_and_die("LLC n. banks (-b) must be a power of 2");

    if (llc_n_ways == 0)
        print_message_and_die("must supply LLC n. ways (-w)");

    if (__builtin_popcountll(llc_n_ways) != 1)
        print_message_and_die("LLC n. ways (-w) must be a power of 2");

    if (llc_allocation_policy == ALLOCATION_POLICY_INVALID)
        print_message_and_die("must specify LLC allocation policy (-a)");

    if (llc_eviction_policy == EVICTION_POLICY_INVALID)
        print_message_and_die("must specify LLC eviction policy (-e)");

    if (rrc_n_lines == 0)
        print_message_and_die("must supply RRC n. lines (-r)");

    if (__builtin_popcountll(rrc_n_lines) != 1)
        print_message_and_die("RRC n. lines (-r) must be a power of 2");

    if (rrc_n_banks == 0)
        print_message_and_die("must supply RRC n. banks (-h)");

    if (__builtin_popcountll(rrc_n_banks) != 1)
        print_message_and_die("RRC n. banks (-h) must be a power of 2");

    if (rrc_n_ways == 0)
        print_message_and_die("must supply RRC n. ways (-k)");

    if (__builtin_popcountll(rrc_n_ways) != 1)
        print_message_and_die("RRC n. ways (-k) must be a power of 2");

    if (rrc_eviction_policy == EVICTION_POLICY_INVALID)
        print_message_and_die("must specify RRC eviction policy (-x)");

    llc_n_lines = llc_size / line_size;

    if (llc_n_lines < llc_n_banks * llc_n_ways)
        print_message_and_die("LLC n. lines must be >= LLC n. banks (-b) times "
                "LLC n. ways (-w)");

    if (rrc_n_lines < rrc_n_banks * rrc_n_ways)
        print_message_and_die("RRC n. lines must be >= RRC n. banks (-h) times "
                "RRC n. ways (-k)");

    lines_per_page = page_size / line_size;

    line_size_log2 = __builtin_ctzll(line_size);
    page_size_log2 = __builtin_ctzll(page_size);

}


void RRLLC::run_pass()
{
    line_addr_t evicted_line_addr;
    access_result_t res;

    mtr.reset();
    while (!mtr.is_end_of_pass()) {
        auto& mt = mtr.next();
        line_addr_t line_addr = mt.line_addr;
        page_addr_t page_addr = line_addr_to_page_addr(line_addr,
                line_size_log2, page_size_log2);
        mem_ref_type_t type = mt.is_write ? MEM_REF_TYPE_ST : MEM_REF_TYPE_LD;

        res = llc->access(line_addr, type, evicted_line_addr);

        // did the core perform a LD? if so, add the page corresponding to the
        // just-read line into the RRC
        if (type == MEM_REF_TYPE_LD) {
            rrc->access(page_addr, MEM_REF_TYPE_ST);
        }

        // did the LLC evict? if so, check if the page corresponding to the
        // just-evicted line was in the RRC
        if (res & ACCESS_RESULT_EVICTION) {
            line_addr_t rrc_line_addr =
                    line_addr_to_page_addr(evicted_line_addr, line_size_log2,
                    page_size_log2);

            rrc->access(rrc_line_addr, MEM_REF_TYPE_LD);
        }
    }
}


void
RRLLC::run()
{
    // first, do a warm-up pass
    run_pass();

    // clear stats, but do not clear warmed-up internal state
    llc->clear_stats();
    rrc->clear_stats();

    // do another pass
    run_pass();
}


void
RRLLC::aggregate_stats()
{
    // aggregate stats for each of the LLC and RRC
    llc->aggregate_stats();
    rrc->aggregate_stats();

    llc_n_hits = llc->get_n_rd_hits() + llc->get_n_wr_hits();
    llc_n_accesses = llc_n_hits +
            llc->get_n_rd_misses() + llc->get_n_wr_misses();
    llc_hit_rate = (double) llc_n_hits / (double) llc_n_accesses;
    llc_n_evictions = llc->get_n_evictions();

    rrc_n_hits = rrc->get_n_rd_hits() + rrc->get_n_wr_hits();
    rrc_n_accesses = rrc_n_hits +
            rrc->get_n_rd_misses() + rrc->get_n_wr_misses();
    rrc_hit_rate = (double) rrc_n_hits / (double) rrc_n_accesses;
    rrc_n_evictions = rrc->get_n_evictions();
}


void
RRLLC::dump_termination_stats()
{
    // using a stringstream, dump to both file and stdout
    std::stringstream ss;

    ss << "LLC_N_HITS" << " " << llc_n_hits << std::endl;
    ss << "LLC_N_ACCESSES" << " " << llc_n_accesses << std::endl;
    ss << "LLC_HIT_RATE" << " " << llc_hit_rate << std::endl;
    ss << "LLC_N_EVICTIONS" << " " << llc_n_evictions << std::endl;

    ss << "RRC_N_HITS" << " " << rrc_n_hits << std::endl;
    ss << "RRC_N_ACCESSES" << " " << rrc_n_hits << std::endl;
    ss << "RRC_HIT_RATE" << " " << rrc_hit_rate << std::endl;
    ss << "RRC_N_EVICTIONS" << " " << rrc_n_evictions << std::endl;

    std::cout << ss.rdbuf()->str();

    std::ofstream ofs("rrllc.txt", std::ofstream::out);
    ofs << ss.rdbuf()->str();
}


int
main(int argc, char* argv[])
{
    RRLLC rl(argc, argv);

    rl.run();
    rl.aggregate_stats();
    rl.dump_termination_stats();

    return 0;
}
