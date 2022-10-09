/*
 * Common definitions for Cache implementation (typedefs, enums, etc.).
 * These are here to get around circular dependency issues with #includes.
 */
#pragma once

#include <stdint.h>

#include <unordered_map>

#include "../../common/defs.h"


typedef enum {
    ALLOCATION_POLICY_AORW,
    ALLOCATION_POLICY_AOWO,
    ALLOCATION_POLICY_INVALID,
} allocation_policy_t;

typedef enum {
    EVICTION_POLICY_LRU,
    EVICTION_POLICY_RANDOM,
    EVICTION_POLICY_INVALID,
} eviction_policy_t;

typedef enum {
    MEM_REF_TYPE_LD,
    MEM_REF_TYPE_ST,
    MEM_REF_TYPE_INVALID,
} mem_ref_type_t;

/*
 * Bitmask. Bit 0 is hit (1) or miss (0); bit 1 is eviction occurred (1) or
 * no eviction occurred (0).
 */
typedef uint8_t access_result_t;
static constexpr access_result_t ACCESS_RESULT_HIT = 0b01;
static constexpr access_result_t ACCESS_RESULT_MISS = 0b00;
static constexpr access_result_t ACCESS_RESULT_EVICTION = 0b10;
static constexpr access_result_t ACCESS_RESULT_NO_EVICTION = 0b00;
static constexpr access_result_t ACCESS_RESULT_INVALID = 0b11111111;
