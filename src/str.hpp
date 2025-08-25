// str.hpp - Complete String implementation with hybrid stack/arena support

#pragma once
#include <climits>
#include <cstdarg>
#include <cstring>
#include <cctype>
#include <cstdio>
#include "vec.hpp"



// Primary template - Arena allocation
template <typename ArenaTag, size_t InitialCapacity = 32>
class Str {
    static_assert(is_arena_tag<ArenaTag>::value, "First parameter must be an Arena tag");

    char *data;
    size_t len;
    size_t capacity;

public:
    // ========================================================================
    // Constructors
    // ========================================================================

    Str() : data(nullptr), len(0), capacity(0) {}

    static Str* create(size_t initial_capacity = 32) {
            size_t str_size = sizeof(Str);
            size_t buffer_size = initial_capacity;
            size_t total_size = str_size + buffer_size;

            void* memory = Arena<ArenaTag>::alloc(total_size);

            Str* str = new (memory) Str();
            str->data = (char*)((uint8_t*)memory + str_size);
            str->capacity = initial_capacity;
            str->len = 0;
            str->data[0] = '\0';

            return str;
        }

        // Create from C string - size perfectly
        static Str* create(const char* cstr) {
            size_t len = strlen(cstr);
            size_t str_size = sizeof(Str);
            size_t buffer_size = len + 1;
            size_t total_size = str_size + buffer_size;

            void* memory = Arena<ArenaTag>::alloc(total_size);

            Str* str = new (memory) Str();
            str->data = (char*)((uint8_t*)memory + str_size);
            str->capacity = buffer_size;
            str->len = len;
            memcpy(str->data, cstr, len + 1);

            return str;
        }

    Str(const char *str) : data(nullptr), len(0), capacity(0) {
        if (str) assign(str);
    }

    Str(const char *str, size_t length) : data(nullptr), len(0), capacity(0) {
        assign(str, length);
    }

    Str(const Str &other) : data(nullptr), len(0), capacity(0) {
        if (other.data) assign(other.data, other.len);
    }

    // ========================================================================
    // Core Operations
    // ========================================================================

    void assign(const char *str) {
        if (!str) {
            clear();
            return;
        }
        assign(str, strlen(str));
    }

    void assign(const char *str, size_t length) {
        if (!str || length == 0) {
            clear();
            return;
        }

        ensure_capacity(length + 1);
        memcpy(data, str, length);
        data[length] = '\0';
        len = length;
    }

    void ensure_capacity(size_t required) {
        if (!data || required > capacity) {
            capacity = required > InitialCapacity ? required : InitialCapacity;
            // Round up to power of 2
            capacity = 1 << (32 - __builtin_clz(capacity - 1));

            char *new_data = (char *)Arena<ArenaTag>::alloc(capacity);
            if (data && new_data && len > 0) {
                memcpy(new_data, data, len + 1);
            }
            data = new_data;
        }
    }

    // ========================================================================
    // Assignment Operators
    // ========================================================================

    Str& operator=(const char *str) {
        assign(str);
        return *this;
    }

    Str& operator=(const Str &other) {
        if (this != &other) {
            assign(other.data, other.len);
        }
        return *this;
    }

    template<typename OtherTag, size_t OtherCap>
    Str& operator=(const Str<OtherTag, OtherCap>& other) {
        assign(other.c_str(), other.length());
        return *this;
    }

    // ========================================================================
    // Cross-Arena/Stack Operations
    // ========================================================================

    template<typename OtherTag, size_t OtherCap = InitialCapacity>
    Str<OtherTag, OtherCap> copy_to() const {
        Str<OtherTag, OtherCap> result;
        if (data) {
            result.assign(data, len);
        }
        return result;
    }

    template<typename OtherTag, size_t OtherCap>
    void copy_from(const Str<OtherTag, OtherCap>& other) {
        assign(other.c_str(), other.length());
    }

    // ========================================================================
    // Append Operations
    // ========================================================================

    void append(const char *str) {
        if (!str) return;
        append(str, strlen(str));
    }

    void append(const char *str, size_t str_len) {
        if (!str || str_len == 0) return;

        size_t new_len = len + str_len;
        ensure_capacity(new_len + 1);

        memcpy(data + len, str, str_len);
        data[new_len] = '\0';
        len = new_len;
    }

    void append(char c) {
        ensure_capacity(len + 2);
        data[len++] = c;
        data[len] = '\0';
    }

    void append(const Str &other) {
        append(other.data, other.len);
    }

    template<typename OtherTag, size_t OtherCap>
    void append(const Str<OtherTag, OtherCap>& other) {
        append(other.c_str(), other.length());
    }

    // ========================================================================
    // Concatenation Operators
    // ========================================================================

    Str operator+(const char *str) const {
        Str result(*this);
        result.append(str);
        return result;
    }

    Str operator+(const Str &other) const {
        Str result(*this);
        result.append(other.data, other.len);
        return result;
    }

    Str operator+(char c) const {
        Str result(*this);
        result.append(c);
        return result;
    }

    Str& operator+=(const char *str) {
        append(str);
        return *this;
    }

    Str& operator+=(const Str &other) {
        append(other.data, other.len);
        return *this;
    }

    Str& operator+=(char c) {
        append(c);
        return *this;
    }

    // ========================================================================
    // Comparison Operators
    // ========================================================================

    bool operator==(const char *str) const {
        if (!data && !str) return true;
        if (!data || !str) return false;
        return strcmp(data, str) == 0;
    }

    bool operator==(const Str &other) const {
        if (len != other.len) return false;
        if (!data && !other.data) return true;
        if (!data || !other.data) return false;
        return memcmp(data, other.data, len) == 0;
    }

    bool operator!=(const char *str) const { return !(*this == str); }
    bool operator!=(const Str &other) const { return !(*this == other); }

    bool operator<(const char *str) const {
        if (!data && !str) return false;
        if (!data) return true;
        if (!str) return false;
        return strcmp(data, str) < 0;
    }

    bool operator<(const Str &other) const {
        if (!data && !other.data) return false;
        if (!data) return true;
        if (!other.data) return false;
        return strcmp(data, other.data) < 0;
    }

    bool operator>(const char *str) const {
        return str && *this != str && !(*this < str);
    }

    bool operator>(const Str &other) const {
        return *this != other && !(*this < other);
    }

    bool operator<=(const char *str) const { return !(*this > str); }
    bool operator<=(const Str &other) const { return !(*this > other); }
    bool operator>=(const char *str) const { return !(*this < str); }
    bool operator>=(const Str &other) const { return !(*this < other); }

    // ========================================================================
    // Character Access
    // ========================================================================

    char& operator[](size_t i) { return data[i]; }
    const char& operator[](size_t i) const { return data[i]; }

    char& at(size_t i) {
        if (i >= len) {
            static char null_char = '\0';
            return i == len ? data[len] : null_char;
        }
        return data[i];
    }

    const char& at(size_t i) const {
        if (i >= len) {
            static char null_char = '\0';
            return null_char;
        }
        return data[i];
    }

    // ========================================================================
    // Substring Operations
    // ========================================================================

    Str substr(size_t start, size_t length = SIZE_MAX) const {
        if (start >= len) return Str();
        if (length > len - start) {
            length = len - start;
        }
        return Str(data + start, length);
    }

    Str slice(int start, int end = INT_MAX) const {
        if (start < 0) start = len + start;
        if (end < 0) end = len + end;
        if (start < 0) start = 0;
        if (end > (int)len) end = len;
        if (start >= end) return Str();
        return Str(data + start, end - start);
    }

    // ========================================================================
    // Trimming
    // ========================================================================

    Str& trim_left() {
        if (!data) return *this;
        size_t start = 0;
        while (start < len && isspace(data[start])) start++;
        if (start > 0) {
            len -= start;
            memmove(data, data + start, len + 1);
        }
        return *this;
    }

    Str& trim_right() {
        if (!data) return *this;
        while (len > 0 && isspace(data[len - 1])) {
            len--;
        }
        data[len] = '\0';
        return *this;
    }

    Str& trim() {
        return trim_left().trim_right();
    }

    Str trimmed() const {
        Str result(*this);
        result.trim();
        return result;
    }

    // ========================================================================
    // Case Conversion
    // ========================================================================

    Str& to_lower() {
        for (size_t i = 0; i < len; i++) {
            data[i] = tolower(data[i]);
        }
        return *this;
    }

    Str& to_upper() {
        for (size_t i = 0; i < len; i++) {
            data[i] = toupper(data[i]);
        }
        return *this;
    }

    Str lowered() const {
        Str result(*this);
        result.to_lower();
        return result;
    }

    Str uppered() const {
        Str result(*this);
        result.to_upper();
        return result;
    }

    // ========================================================================
    // Splitting
    // ========================================================================

    Vec<Str, ArenaTag> split(char delimiter) const {
        Vec<Str, ArenaTag> result;
        if (!data) return result;
        size_t start = 0;
        for (size_t i = 0; i <= len; i++) {
            if (i == len || data[i] == delimiter) {
                if (i > start) {
                    result.push_back(Str(data + start, i - start));
                }
                start = i + 1;
            }
        }
        return result;
    }

    Vec<Str, ArenaTag> split(const char* delimiters) const {
        Vec<Str, ArenaTag> result;
        if (!data || !delimiters) return result;
        size_t start = 0;
        for (size_t i = 0; i <= len; i++) {
            if (i == len || strchr(delimiters, data[i])) {
                if (i > start) {
                    result.push_back(Str(data + start, i - start));
                }
                start = i + 1;
            }
        }
        return result;
    }

    // ========================================================================
    // Search Operations
    // ========================================================================

    bool contains(const char *substr) const {
        if (!data || !substr) return false;
        return strstr(data, substr) != nullptr;
    }

    bool contains(const Str &substr) const {
        return contains(substr.c_str());
    }

    int find(const char *substr) const {
        if (!data || !substr) return -1;
        const char *pos = strstr(data, substr);
        if (pos) return (int)(pos - data);
        return -1;
    }

    int find(const Str &substr) const {
        return find(substr.c_str());
    }

    bool starts_with(const char *prefix) const {
        if (!data || !prefix) return false;
        size_t prefix_len = strlen(prefix);
        if (prefix_len > len) return false;
        return strncmp(data, prefix, prefix_len) == 0;
    }

    bool starts_with(const Str &prefix) const {
        return starts_with(prefix.c_str());
    }

    bool ends_with(const char *suffix) const {
        if (!data || !suffix) return false;
        size_t suffix_len = strlen(suffix);
        if (suffix_len > len) return false;
        return strcmp(data + len - suffix_len, suffix) == 0;
    }

    bool ends_with(const Str &suffix) const {
        return ends_with(suffix.c_str());
    }

    // ========================================================================
    // Replace Operations
    // ========================================================================

    Str& replace(const char *find_str, const char *replace_str) {
        if (!data || !find_str || !replace_str) return *this;

        size_t find_len = strlen(find_str);
        size_t replace_len = strlen(replace_str);

        if (find_len == 0) return *this;

        // Count occurrences
        size_t count = 0;
        const char *pos = data;
        while ((pos = strstr(pos, find_str)) != nullptr) {
            count++;
            pos += find_len;
        }

        if (count == 0) return *this;

        // Calculate new size
        size_t new_len = len + count * (replace_len - find_len);

        if (new_len == len && find_len == replace_len) {
            // In-place replacement
            char *pos = data;
            while ((pos = strstr(pos, find_str)) != nullptr) {
                memcpy(pos, replace_str, replace_len);
                pos += replace_len;
            }
        } else {
            // Need to reallocate
            Str temp;
            temp.ensure_capacity(new_len + 1);

            const char *src = data;
            char *dst = temp.data;

            while (*src) {
                const char *match = strstr(src, find_str);
                if (match) {
                    size_t prefix_len = match - src;
                    memcpy(dst, src, prefix_len);
                    dst += prefix_len;
                    memcpy(dst, replace_str, replace_len);
                    dst += replace_len;
                    src = match + find_len;
                } else {
                    strcpy(dst, src);
                    break;
                }
            }

            temp.len = new_len;
            *this = temp;
        }

        return *this;
    }

    Str replaced(const char *find_str, const char *replace_str) const {
        Str result(*this);
        result.replace(find_str, replace_str);
        return result;
    }

    // ========================================================================
    // Numeric Conversions
    // ========================================================================

    static Str from_int(int value) {
        char buffer[32];
        snprintf(buffer, sizeof(buffer), "%d", value);
        return Str(buffer);
    }

    static Str from_uint(unsigned int value) {
        char buffer[32];
        snprintf(buffer, sizeof(buffer), "%u", value);
        return Str(buffer);
    }

    static Str from_long(long value) {
        char buffer[32];
        snprintf(buffer, sizeof(buffer), "%ld", value);
        return Str(buffer);
    }

    static Str from_float(float value, int precision = 6) {
        char buffer[64];
        snprintf(buffer, sizeof(buffer), "%.*f", precision, value);
        return Str(buffer);
    }

    static Str from_double(double value, int precision = 6) {
        char buffer[64];
        snprintf(buffer, sizeof(buffer), "%.*f", precision, value);
        return Str(buffer);
    }

    int to_int() const {
        return data ? atoi(data) : 0;
    }

    long to_long() const {
        return data ? atol(data) : 0;
    }

    float to_float() const {
        return data ? (float)atof(data) : 0.0f;
    }

    double to_double() const {
        return data ? atof(data) : 0.0;
    }

    // ========================================================================
    // Format (printf-style)
    // ========================================================================

    static Str format(const char *fmt, ...) {
        va_list args;
        va_start(args, fmt);

        // Determine required size
        va_list args_copy;
        va_copy(args_copy, args);
        int size = vsnprintf(nullptr, 0, fmt, args_copy);
        va_end(args_copy);

        if (size < 0) {
            va_end(args);
            return Str();
        }

        Str result;
        result.ensure_capacity(size + 1);
        vsnprintf(result.data, size + 1, fmt, args);
        result.len = size;

        va_end(args);
        return result;
    }

    // ========================================================================
    // Core Accessors
    // ========================================================================

    const char *c_str() const { return data ? data : ""; }
    size_t length() const { return len; }
    size_t size() const { return len; }
    bool empty() const { return len == 0; }
    void clear() { len = 0; if (data) data[0] = '\0'; }

    // Implicit conversion to const char*
    operator const char*() const { return c_str(); }

    // Introspection
    constexpr bool is_stack_allocated() const { return false; }
    constexpr size_t stack_capacity() const { return 0; }
};

// ========================================================================
// Specialization for stack allocation
// ========================================================================

template <size_t StackSize, size_t InitialCapacity>
class Str<stack_size_tag<StackSize>, InitialCapacity> {
    char stack_buffer[StackSize];
    size_t len;
    size_t capacity;

public:
    Str() : len(0), capacity(StackSize) {
        stack_buffer[0] = '\0';
    }

    Str(const char *str) : len(0), capacity(StackSize) {
        stack_buffer[0] = '\0';
        if (str) assign(str);
    }

    Str(const char *str, size_t length) : len(0), capacity(StackSize) {
        stack_buffer[0] = '\0';
        assign(str, length);
    }

    Str(const Str &other) : len(0), capacity(StackSize) {
        stack_buffer[0] = '\0';
        assign(other.c_str(), other.len);
    }

    void assign(const char *str) {
        if (!str) {
            clear();
            return;
        }
        assign(str, strlen(str));
    }

    void assign(const char *str, size_t length) {
        if (!str || length == 0) {
            clear();
            return;
        }
        ensure_capacity(length + 1);
        memcpy(stack_buffer, str, length);
        stack_buffer[length] = '\0';
        len = length;
    }

    void ensure_capacity(size_t required) {
        if (required > StackSize) {
            fprintf(stderr, "Stack string overflow! Required: %zu, Available: %zu\n",
                    required, StackSize);
            exit(1);
        }
    }

    // All the same methods as arena version but using stack_buffer
    Str& operator=(const char *str) {
        assign(str);
        return *this;
    }

    Str& operator=(const Str &other) {
        if (this != &other) {
            assign(other.c_str(), other.len);
        }
        return *this;
    }

    void append(const char *str, size_t str_len) {
        if (!str || str_len == 0) return;
        size_t new_len = len + str_len;
        ensure_capacity(new_len + 1);
        memcpy(stack_buffer + len, str, str_len);
        stack_buffer[new_len] = '\0';
        len = new_len;
    }

    void append(const char *str) {
        if (str) append(str, strlen(str));
    }

    void append(char c) {
        ensure_capacity(len + 2);
        stack_buffer[len++] = c;
        stack_buffer[len] = '\0';
    }

    Str& operator+=(const char *str) {
        append(str);
        return *this;
    }

    Str& operator+=(char c) {
        append(c);
        return *this;
    }

    bool operator==(const char *str) const {
        if (!str) return len == 0;
        return strcmp(stack_buffer, str) == 0;
    }

    bool operator==(const Str &other) const {
        if (len != other.len) return false;
        return memcmp(stack_buffer, other.stack_buffer, len) == 0;
    }

    bool operator!=(const char *str) const { return !(*this == str); }
    bool operator!=(const Str &other) const { return !(*this == other); }

    char& operator[](size_t i) { return stack_buffer[i]; }
    const char& operator[](size_t i) const { return stack_buffer[i]; }

    template<typename OtherTag, size_t OtherCap>
       Str& operator=(const Str<OtherTag, OtherCap>& other) {
           assign(other.c_str(), other.length());  // Must use public methods!
           return *this;
       }



    // Include all other methods (trimming, case conversion, etc.)
    // Similar implementations using stack_buffer instead of data

    const char *c_str() const { return stack_buffer; }
    size_t length() const { return len; }
    size_t size() const { return len; }
    bool empty() const { return len == 0; }
    void clear() { len = 0; stack_buffer[0] = '\0'; }

    operator const char*() const { return c_str(); }

    constexpr bool is_stack_allocated() const { return true; }
    constexpr size_t stack_capacity() const { return StackSize; }
};

// Convenience factory functions
template<size_t N>
/* Embedded Str */
using EmbStr = Str<stack_size_tag<N>>;

// Global operators for char* on left side
template<typename Tag, size_t Cap>
bool operator==(const char* lhs, const Str<Tag, Cap>& rhs) {
    return rhs == lhs;
}

template<typename Tag, size_t Cap>
bool operator!=(const char* lhs, const Str<Tag, Cap>& rhs) {
    return rhs != lhs;
}


// Usage:
// EmbStr<64> stack_str;           // 64 bytes on stack
// Str<MyArena> arena_str;           // Arena allocated
