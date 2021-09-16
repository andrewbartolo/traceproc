/*
 * Misc. utilities.
 */
#pragma once

#include <string>
#include <unordered_map>


#define MAX(a, b) (a > b ? a : b)
#define MIN(a, b) (a < b ? a : b)

void print_message_and_die(const char* format, ...);
int64_t shorthand_to_integer(std::string s, const size_t b);
int string_to_boolean(std::string s);
std::unordered_map<std::string, std::string>
        parse_kv_file(std::string input_filepath);
