// Add to a new file: src/pattern.hpp
#pragma once
#include <cctype>
#include <cstring>
#include <cstdint>

// Check if a string matches a SQL LIKE pattern
// Supports: % (any sequence), _ (any single char), \ (escape)
inline bool evaluate_like_pattern(const uint8_t* str, const uint8_t* pattern,
                                  uint32_t str_len, uint32_t pattern_len) {
    uint32_t s = 0, p = 0;
    uint32_t star_s = UINT32_MAX, star_p = UINT32_MAX;

    // Remove trailing spaces for VARCHAR comparison
    while (str_len > 0 && str[str_len - 1] == ' ') str_len--;
    while (pattern_len > 0 && pattern[pattern_len - 1] == ' ') pattern_len--;

    while (s < str_len) {
        if (p < pattern_len && pattern[p] == '%') {
            // Save position for backtracking
            star_p = p++;
            star_s = s;
        }
        else if (p < pattern_len &&
                (pattern[p] == '_' || pattern[p] == str[s])) {
            // Single char match
            p++;
            s++;
        }
        else if (star_p != UINT32_MAX) {
            // Backtrack to last %
            p = star_p + 1;
            s = ++star_s;
        }
        else {
            return false;
        }
    }

    // Consume trailing %
    while (p < pattern_len && pattern[p] == '%') p++;

    return p == pattern_len;
}

// Check if pattern is a simple prefix match (optimization opportunity)
inline bool is_prefix_pattern(const uint8_t* pattern, uint32_t len) {
    // Pattern like 'Bob%' with no other wildcards
    for (uint32_t i = 0; i < len; i++) {
        if (pattern[i] == '%') {
            // Check if % is at end (ignoring trailing spaces)
            for (uint32_t j = i + 1; j < len; j++) {
                if (pattern[j] != '%' && pattern[j] != ' ') {
                    return false;  // % not at end
                }
            }
            return i > 0;  // Has prefix before %
        }
        if (pattern[i] == '_') {
            return false;  // Has other wildcards
        }
    }
    return false;  // No wildcards at all
}

// Extract prefix from pattern like 'Bob%' -> 'Bob'
inline uint32_t get_prefix_length(const uint8_t* pattern, uint32_t len) {
    for (uint32_t i = 0; i < len; i++) {
        if (pattern[i] == '%' || pattern[i] == '_') {
            return i;
        }
    }
    return len;
}

// Helper for case-insensitive LIKE (ILIKE)
inline bool evaluate_ilike_pattern(const uint8_t* str, const uint8_t* pattern,
                                   uint32_t str_len, uint32_t pattern_len) {
    // Convert to uppercase and compare
    uint8_t str_upper[256], pattern_upper[256];

    for (uint32_t i = 0; i < str_len && i < 256; i++) {
        str_upper[i] = toupper(str[i]);
    }
    for (uint32_t i = 0; i < pattern_len && i < 256; i++) {
        pattern_upper[i] = toupper(pattern[i]);
    }

    return evaluate_like_pattern(str_upper, pattern_upper,
                                 str_len < 256 ? str_len : 256,
                                 pattern_len < 256 ? pattern_len : 256);
}
