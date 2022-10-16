/*
 * NOTE: many member functions are declared as inline and defined in the
 * accompanying .h file.
 */
#include <filesystem>
#include <stdexcept>
#include <unordered_map>

#include "MemTraceReader.h"
#include "defs.h"
#include "util.h"


MemTraceReader::MemTraceReader()
{
}


MemTraceReader::~MemTraceReader()
{
    if (ifs.is_open()) ifs.close();
    if (buf != nullptr) delete buf;
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
    ifs.open(input_filepath, std::ios::binary);

    // find the size of the file
    ifs.seekg(0, std::ios_base::end);
    input_file_n_bytes = ifs.tellg();
    // and reset to beginning
    ifs.seekg(0, std::ios_base::beg);

    if (input_file_n_bytes % sizeof(memtrace_entry_t) != 0)
        throw std::runtime_error("incorrect or corrupt input memtrace file");

    n_unique_entries = input_file_n_bytes / sizeof(memtrace_entry_t);

    // see how large of a buffer we should allocate
    char* requested_buffer_size_str =
            std::getenv("TRACEPROC_TRACE_BUFFER_SIZE");
    size_t requested_buffer_size_bytes = requested_buffer_size_str ?
            shorthand_to_integer(requested_buffer_size_str, 1024) :
            DEFAULT_REQUESTED_BUFFER_SIZE_BYTES;

    // we ensure that buffer_size_bytes % sizeof(memtrace_entry_t) == 0
    buffer_size_entries = requested_buffer_size_bytes /
            sizeof(memtrace_entry_t);
    buffer_size_bytes = buffer_size_entries * sizeof(memtrace_entry_t);
    printf("trace buffer size (bytes): %zu\n", buffer_size_bytes);

    // allocate a buf of the size determined above
    buf = new memtrace_entry_t[buffer_size_entries];

    refill(true /* force */);
}


void
MemTraceReader::get_first_entry(memtrace_entry_t& entry)
{
    off_t curr_offset = ifs.tellg();
    ifs.seekg(0, std::ios_base::beg);
    ifs.read((char*) &entry, sizeof(entry));
    ifs.seekg(curr_offset, std::ios_base::beg);
}


void
MemTraceReader::get_last_entry(memtrace_entry_t& entry)
{
    off_t curr_offset = ifs.tellg();
    ifs.seekg(-sizeof(entry), std::ios_base::end);
    ifs.read((char*) &entry, sizeof(entry));
    ifs.seekg(curr_offset, std::ios_base::beg);
}
