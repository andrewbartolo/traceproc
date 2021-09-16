/*
 * NOTE: many member functions are declared as inline and defined in the
 * accompanying .h file.
 */
#include <filesystem>
#include <stdexcept>
#include <unordered_map>

#include "MemTraceReader.h"
#include "defs.h"


MemTraceReader::MemTraceReader()
{
}


void
MemTraceReader::load(const std::string& input_filepath)
{
    this->input_filepath = input_filepath;

    // check that filepath exists
    std::filesystem::path path(input_filepath);
    if (!std::filesystem::exists(path))
        throw std::runtime_error(input_filepath + " does not exist");

    // open the ifstream
    std::ifstream ifs(input_filepath, std::ios::binary);

    // find the size of the file
    ifs.seekg(0, std::ios_base::end);
    input_file_n_bytes = ifs.tellg();
    // and reset to beginning
    ifs.seekg(0, std::ios_base::beg);

    if (input_file_n_bytes % sizeof(memtrace_entry_t) != 0)
        throw std::runtime_error("incorrect or corrupt input memtrace file");

    n_unique_entries = input_file_n_bytes / sizeof(memtrace_entry_t);

    // allocate a buf and read the whole file into it
    // NOTE: this will require a *lot* of memory!
    buf = std::make_unique<memtrace_entry_t[]>(n_unique_entries);

    ifs.read((char*) buf.get(), input_file_n_bytes);

    std::unordered_map<page_addr_t, uint64_t> m;

    // iterate through once to get some info about the trace
    for (size_t i = 0; i < n_unique_entries; ++i) {
        buf[i].is_write ? ++n_writes_in_trace : ++n_reads_in_trace;
        if (buf[i].is_write) m[buf[i].line_addr >> 14] += 1;
    }

    // TODO make these into a library call
    uint64_t max = 0;
    uint64_t min = 999999999999999;
    for (auto& kv : m) {
        if (kv.second > max) max = kv.second;
        if (kv.second < min) min = kv.second;
    }

    printf("MAX page write count: %zu\n", max);
    printf("MIN page write count: %zu\n", min);
}
