/*
 * Utility class for (cyclically) reading traces from a memtrace.bin file.
 * NOTE: many member functions are declared as inline and defined in this .
 * file.
 * NOTE 2: because MemTraceReader reads an entire multi-gigabyte trace file
 * into memory, it requires a lot of RAM.
 * FUTURE: consider adding an alternate mode that uses un-user-buffered ifstream
 * (in testing this was ~2X slower).
 */
#pragma once

#include <cstdbool>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <string>
#include <memory>

#include "defs.h"


class MemTraceReader {
    public:
        typedef struct __attribute__((packed)) {
            unsigned node_num:15;
            unsigned is_write:1;
            line_addr_t line_addr:64;
        } memtrace_entry_t;

        MemTraceReader();
        void load(const std::string& input_filepath);
        inline memtrace_entry_t& next();
        inline bool is_end_of_pass();
        inline uint64_t get_n_requests();
        inline uint64_t get_n_full_passes();
        inline uint64_t get_n_unique_entries();
        inline uint64_t get_n_reads_in_trace();
        inline uint64_t get_n_writes_in_trace();

        // reset all counters and stats
        void reset(bool inc_passes=true);

        // static helper methods
        static inline page_addr_t line_addr_to_page_addr(line_addr_t line_addr,
                uint64_t line_size_log2, uint64_t page_size_log2);

    private:
        std::string input_filepath;
        std::unique_ptr<memtrace_entry_t[]> buf;

        size_t input_file_n_bytes = 0;
        size_t n_unique_entries = 0;
        size_t n_reads_in_trace = 0;
        size_t n_writes_in_trace = 0;
        uint64_t n_requests = 0;
        uint64_t n_full_passes = 0;
        size_t curr = 0;
};


inline MemTraceReader::memtrace_entry_t&
MemTraceReader::next()
{
    if (is_end_of_pass()) reset(true);

    ++n_requests;
    return buf[curr++];
}


inline bool
MemTraceReader::is_end_of_pass()
{
    return curr == n_unique_entries;
}


inline uint64_t
MemTraceReader::get_n_requests()
{
    return n_requests;
}


inline uint64_t
MemTraceReader::get_n_full_passes()
{
    return n_full_passes;
}


inline size_t
MemTraceReader::get_n_unique_entries()
{
    return n_unique_entries;
}


inline size_t
MemTraceReader::get_n_reads_in_trace()
{
    return n_reads_in_trace;
}


inline size_t
MemTraceReader::get_n_writes_in_trace()
{
    return n_writes_in_trace;
}


inline void
MemTraceReader::reset(bool inc_passes)
{
    curr = 0;

    if (inc_passes) ++n_full_passes;
}


static inline page_addr_t
line_addr_to_page_addr(line_addr_t line_addr, uint64_t line_size_log2,
        uint64_t page_size_log2)
{
    return line_addr >> (page_size_log2 - line_size_log2);
}
