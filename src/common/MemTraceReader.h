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
            unsigned long cycle:64;
        } memtrace_entry_t;

        MemTraceReader();
        ~MemTraceReader();
        void load(const std::string& input_filepath);
        inline memtrace_entry_t& next();
        inline bool is_end_of_buffer();
        inline bool is_end_of_pass();
        inline uint64_t get_n_requests();
        inline uint64_t get_n_full_passes();
        inline uint64_t get_n_unique_entries();
        void get_first_entry(memtrace_entry_t& entry);
        void get_last_entry(memtrace_entry_t& entry);

        void reset();

        // static helper methods
        static inline page_addr_t line_addr_to_page_addr(line_addr_t line_addr,
                uint64_t line_size_log2, uint64_t page_size_log2);

    private:
        void refill(bool force=false);

        // default buffer size: ~8 GiB
        static constexpr size_t DEFAULT_REQUESTED_BUFFER_SIZE_BYTES =
                8589934592;

        std::string input_filepath;
        std::ifstream ifs;
        memtrace_entry_t* buf = nullptr;

        size_t input_file_n_bytes = 0;
        size_t n_unique_entries = 0;
        size_t buffer_size_bytes = 0;
        size_t buffer_size_entries = 0;
        uint64_t n_requests = 0;
        uint64_t n_full_passes = 0;
        size_t buffer_curr_entry = 0;
        size_t full_trace_entry_ctr = 0;
};


inline MemTraceReader::memtrace_entry_t&
MemTraceReader::next()
{
    if (is_end_of_buffer()) {
        refill();
    }

    if (is_end_of_pass()) {
        ++n_full_passes;
        full_trace_entry_ctr = 0;
    }
    else ++full_trace_entry_ctr;

    ++n_requests;
    return buf[buffer_curr_entry++];
}


inline bool
MemTraceReader::is_end_of_buffer()
{
    return buffer_curr_entry == buffer_size_entries;
}


inline bool
MemTraceReader::is_end_of_pass()
{
    return full_trace_entry_ctr == n_unique_entries;
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


inline void
MemTraceReader::refill(bool force)
{
    printf("refilling...\n");
    // first, reset the buffer curr pointer
    buffer_curr_entry = 0;

    // no need to re-read if entire trace fits in buffer
    // (with the exception of the very first read, which has force=true)
    if (!force && n_unique_entries <= buffer_size_entries) return;
    printf("refilling-reading...\n");


    // if we got here, need to read into the buffer.

    size_t bytes_till_end_of_file = input_file_n_bytes - ifs.tellg();

    if (bytes_till_end_of_file >= buffer_size_bytes) {
        // can just read in one part
        ifs.read(((char*) buf), buffer_size_bytes);
    }
    else {
        // must read in two parts:
        // 1. from curr file pos to end of file
        // 2. from beginning of file to end of buffer space
        ifs.read(((char*) buf), bytes_till_end_of_file);
        ifs.seekg(0, std::ios_base::beg);
        size_t remaining_bytes = buffer_size_bytes - bytes_till_end_of_file;
        ifs.read(((char*) buf) + bytes_till_end_of_file, remaining_bytes);
    }
}


inline void
MemTraceReader::reset()
{
    // reset the visible state of "doing a full pass" through the trace
    // buffer curr, full-trace ctr, and file offset all go to 0
    buffer_curr_entry = 0;
    full_trace_entry_ctr = 0;
    ifs.seekg(0, std::ios_base::beg);

    // force a fresh, aligned read
    refill(true /* force */);
}


static inline page_addr_t
line_addr_to_page_addr(line_addr_t line_addr, uint64_t line_size_log2,
        uint64_t page_size_log2)
{
    return line_addr >> (page_size_log2 - line_size_log2);
}
