/*
 * Takes in multiple input arguments, including
 * 1. event (timestamp) trace file, and 
 * 2. time for one event to elapse,
 * and tracks the maximum queue occupancy for events awaiting processing.
 */
#pragma once

#include <cstdint>
#include <list>
#include <memory>
#include <string>

#include "../common/defs.h"


typedef enum {
    EVENTTRACE_TYPE_INVALID,
    EVENTTRACE_TYPE_UINT64,
    EVENTTRACE_TYPE_FLOAT64,
} eventtrace_type_t;

typedef struct {
    std::string trace_filepath;
    eventtrace_type_t type;
    uint64_t event_duration_u64;
    double event_duration_f64;
} eventtrace_args_t;

static void parse_and_validate_args(int argc, char* argv[],
        eventtrace_args_t& args);


template <typename T>
class EventTrace {
    public:
        EventTrace<T>(const eventtrace_args_t& args);
        EventTrace<T>(const EventTrace& snq) = delete;
        EventTrace<T>& operator=(const EventTrace& snq) = delete;
        EventTrace<T>(EventTrace&& snq) = delete;
        EventTrace<T>& operator=(EventTrace&& snq) = delete;
        ~EventTrace<T>();

        void run();
        void dump_stats();

    private:
        // input arguments
        std::string trace_filepath;
        std::string type_str;
        T event_duration;

        // derived, or from input files
        size_t trace_file_n_bytes;
        size_t n_unique_entries;

        // internal mechanics
        std::unique_ptr<T[]> buf;
        std::list<T> start_times;
        uint64_t max_queue_depth = 0;
};
