#include <algorithm>
#include <array>
#include <cstdio>
#include <iterator>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <unistd.h>

#include "../common/util.h"
#include "EventTrace.h"


template <typename T>
EventTrace<T>::EventTrace(const eventtrace_args_t& args)
{
    // fill member variables with args
    this->trace_filepath = args.trace_filepath;
    if (args.type == EVENTTRACE_TYPE_UINT64) {
        this->type_str = "UINT64";
        this->event_duration = args.event_duration_u64;
    }
    else if (args.type == EVENTTRACE_TYPE_FLOAT64) {
        this->type_str = "FLOAT64";
        this->event_duration = args.event_duration_f64;
    }


    // open the backing trace file ifstream
    std::ifstream ifs(trace_filepath, std::ios::binary);

    // find the size of the file
    ifs.seekg(0, std::ios_base::end);
    trace_file_n_bytes = ifs.tellg();
    // and reset to beginning
    ifs.seekg(0, std::ios_base::beg);

    if (trace_file_n_bytes % sizeof(T) != 0)
        print_message_and_die("incorrect or corrupt input trace file");

    n_unique_entries = trace_file_n_bytes / sizeof(T);

    // allocate a buf and read the whole file into it
    // NOTE: this will require a *lot* of memory!
    buf = std::make_unique<T[]>(n_unique_entries);

    ifs.read((char*) buf.get(), trace_file_n_bytes);

    // sort the input array, as the generated trace may contain event timestamps
    // in not-strictly-ascending order
    std::sort(buf.get(), buf.get() + n_unique_entries);
}


template <typename T>
EventTrace<T>::~EventTrace()
{
}


static void
parse_and_validate_args(int argc, char* argv[],
        eventtrace_args_t& args)
{
    int c;
    optind = 0; // global: clear previous getopt() state, if any
    opterr = 0; // global: don't explicitly warn on unrecognized args
    int n_args_parsed = 0;

    // sentinels
    args.trace_filepath = "";
    args.type = EVENTTRACE_TYPE_INVALID;
    args.event_duration_u64 = 0;
    args.event_duration_f64 = 0.0;

    // temporary helper vars
    std::string type_str;

    // parse
    while ((c = getopt(argc, argv, "f:t:d:")) != -1) {
        try {
            switch (c) {
                case 'f':
                    args.trace_filepath = optarg;
                    break;
                case 't':
                    type_str = optarg;
                    std::transform(type_str.begin(), type_str.end(),
                            type_str.begin(), ::tolower);

                    if (type_str.find("int") != std::string::npos)
                        args.type = EVENTTRACE_TYPE_UINT64;
                    if (type_str.find("float") != std::string::npos)
                        args.type = EVENTTRACE_TYPE_FLOAT64;
                    break;
                case 'd':
                    if (args.type == EVENTTRACE_TYPE_UINT64)
                        args.event_duration_u64 =
                                shorthand_to_integer(optarg, 1000);
                    else if (args.type == EVENTTRACE_TYPE_FLOAT64)
                        args.event_duration_f64 = std::stod(optarg);
                    else
                        print_message_and_die("must supply type (-t) before "
                                "duration (-d)");
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

    if (args.trace_filepath == "")
        print_message_and_die("must supply trace filepath (-f)");

    // check that filepath exists
    std::filesystem::path path(args.trace_filepath);
    if (!std::filesystem::exists(path))
        print_message_and_die("%s does not exist", args.trace_filepath.c_str());

    if (args.type == EVENTTRACE_TYPE_INVALID)
        print_message_and_die("must supply trace type (-t <uint64|float64>)");

    if (args.event_duration_u64 == 0 and args.event_duration_f64 == 0.0)
        print_message_and_die("must supply nonzero event duration in %s "
                "(-d)", type_str.c_str());
}


template <typename T>
void
EventTrace<T>::run()
{
    // main loop. iterate through and see how many entries pile up in the queue,
    // expiring them as they complete
    for (size_t i = 0; i < n_unique_entries; ++i) {
        uint64_t timestamp = buf.get()[i];

        // first, append this start time to the queue
        start_times.emplace_front(timestamp);

        // now, see if we can expire any old ones
        start_times.remove_if([=](uint64_t start_time) {
            return start_time + event_duration <= timestamp;
        });

        // we say that the queue depth doesn't count the
        // currently-being-processed element
        uint64_t queue_depth = start_times.size() - 1;

        // update the max queue depth
        if (max_queue_depth < queue_depth) max_queue_depth = queue_depth;
    }
}


template <typename T>
void
EventTrace<T>::dump_stats()
{
    std::stringstream ss;

    ss << "INPUT_TRACE_TYPE" << " " << type_str << std::endl;
    ss << "EVENT_DURATION" << " " << event_duration << std::endl;
    ss << "MAX_QUEUE_DEPTH" << " " << max_queue_depth << std::endl;

    // output to stdout
    std::cout << ss.rdbuf()->str();

    // and also to a file
    std::ofstream ofs("eventtrace.txt", std::ofstream::out);
    ofs << ss.rdbuf()->str();
}


int
main(int argc, char* argv[])
{
    // because we want a polymorphic (uint64/float64) EventTrace, we make
    // parse_and_validate_args() static, then feed args to a templatized ctor
    eventtrace_args_t args;
    parse_and_validate_args(argc, argv, args);

    if (args.type == EVENTTRACE_TYPE_UINT64) {
        EventTrace<uint64_t> et(args);
        et.run();
        et.dump_stats();
    }
    else if (args.type == EVENTTRACE_TYPE_FLOAT64) {
        EventTrace<double> et(args);
        et.run();
        et.dump_stats();
    }

    return 0;
}
