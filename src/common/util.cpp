#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <regex>
#include <vector>

#include "util.h"


void
print_message_and_die(const char* format, ...)
{
    va_list argptr;
    va_start(argptr, format);
    fprintf(stderr, "ERROR: ");
    vfprintf(stderr, format, argptr);
    fprintf(stderr, "\n");
    va_end(argptr);
    exit(1);
}


/*
 * Parses shorthand strings, e.g., "20B" for  20 billion, to the corresponding
 * int64. TODO: check for overflow! 50+ year durations, etc. may overflow!
 */
int64_t
shorthand_to_integer(std::string s, const size_t b)
{
    assert(b == 1000 or b == 1024);

    char lastChar = std::toupper(s[s.size()-1]);
    int64_t multiplier = 1;

    if      (lastChar == 'K')                    multiplier = b;
    else if (lastChar == 'M')                    multiplier = b*b;
    else if (lastChar == 'B' or lastChar == 'G') multiplier = b*b*b;
    else if (lastChar == 'T')                    multiplier = b*b*b*b;
    else if (lastChar == 'Q')                    multiplier = b*b*b*b*b;

    if (multiplier != 1) s.pop_back();  // trim the last character

    // parse the string
    int64_t mant = std::stoll(s);

    return mant * multiplier;
}


/*
 * Parse a human-supplied string into a boolean value.
 * Returns 0 if false, 1 if true, and -1 if couldn't parse.
 */
int string_to_boolean(std::string s) {
    // convert to all-lowercase for comparison
    for (size_t i = 0; i < s.size(); ++i) s[i] = std::tolower(s[i]);

    // big jump table
    if (s == "e")           return 1;
    if (s == "enabled")     return 1;
    if (s == "on")          return 1;
    if (s == "t")           return 1;
    if (s == "true")        return 1;
    if (s == "y")           return 1;
    if (s == "yes")         return 1;
    if (s == "1")           return 1;

    if (s == "d")           return 0;
    if (s == "disabled")    return 0;
    if (s == "off")         return 0;
    if (s == "f")           return 0;
    if (s == "false")       return 0;
    if (s == "n")           return 0;
    if (s == "no")          return 0;
    if (s == "0")           return 0;

    return -1;
}


/*
 * Parse a basic input .txt file, of the form:
 * KEY0  VAL0
 * KEY1  VAL1
 * ...
 * into a hashmap with keys and values (both keys and values as strings).
 */
std::unordered_map<std::string, std::string>
parse_kv_file(std::string input_filepath)
{
    // first, open the file and read it into a string
    std::ifstream f(input_filepath);
    std::stringstream ss;
    ss << f.rdbuf();
    std::string s = ss.str();

    // now, prepare to tokenize it
    std::unordered_map<std::string, std::string> map;

    std::regex newline_reg("\\\n");
    std::regex whitespace_reg("\\s+");

    std::sregex_token_iterator lines_it(s.begin(), s.end(), newline_reg, -1);
    std::sregex_token_iterator lines_end;
    std::vector<std::string> lines(lines_it, lines_end);

    for (auto& l : lines) {
        std::sregex_token_iterator kv_it(l.begin(), l.end(), whitespace_reg,
                -1);
        std::sregex_token_iterator kv_end;
        std::vector<std::string> kv(kv_it, kv_end);

        map[kv[0]] = kv[1];
    }

    return map;
}
