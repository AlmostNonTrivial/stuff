#include <climits>
#include <cstdarg>
#include <cstring>
#include <cctype>
#include "vec.hpp"
#include "arena.hpp"

template <typename ArenaTag, size_t InitialCapacity = 32>
struct Str {
    char *data;
    size_t len;
    size_t capacity;

    // ========================================================================
    // Constructors
    // ========================================================================

    Str() : data(nullptr), len(0), capacity(0) {}

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

    template<typename OtherArenaTag>
    Str& operator=(const Str<OtherArenaTag>& other) {
        assign(other.c_str(), other.length());
        return *this;
    }

    // ========================================================================
    // Cross-Arena Operations
    // ========================================================================

    template<typename OtherArenaTag>
    Str<OtherArenaTag> copy_to_arena() const {
        Str<OtherArenaTag> result;
        if (data) {
            result.assign(data, len);
        }
        return result;
    }

    template<typename OtherArenaTag>
    void copy_from(const Str<OtherArenaTag>& other) {
        assign(other.c_str(), other.length());
    }

    template<typename OtherArenaTag>
    static Str from_other(const Str<OtherArenaTag>& other) {
        Str result;
        result.assign(other.c_str(), other.length());
        return result;
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

    template<typename OtherArenaTag>
    void append(const Str<OtherArenaTag>& other) {
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

    template<typename OtherArenaTag>
    Str operator+(const Str<OtherArenaTag>& other) const {
        Str result(*this);
        result.append(other.c_str(), other.length());
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

    template<typename OtherArenaTag>
    Str& operator+=(const Str<OtherArenaTag>& other) {
        append(other.c_str(), other.length());
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

    template<typename OtherArenaTag>
    bool operator==(const Str<OtherArenaTag>& other) const {
        if (len != other.length()) return false;
        return memcmp(data, other.c_str(), len) == 0;
    }

    bool operator!=(const char *str) const { return !(*this == str); }
    bool operator!=(const Str &other) const { return !(*this == other); }

    template<typename OtherArenaTag>
    bool operator!=(const Str<OtherArenaTag>& other) const {
        return !(*this == other);
    }

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

    template<typename OtherArenaTag>
    bool operator<(const Str<OtherArenaTag>& other) const {
        return strcmp(c_str(), other.c_str()) < 0;
    }

    bool operator>(const char *str) const {
        return str && *this != str && !(*this < str);
    }

    bool operator>(const Str &other) const {
        return *this != other && !(*this < other);
    }

    template<typename OtherArenaTag>
    bool operator>(const Str<OtherArenaTag>& other) const {
        return strcmp(c_str(), other.c_str()) > 0;
    }

    bool operator<=(const char *str) const { return !(*this > str); }
    bool operator<=(const Str &other) const { return !(*this > other); }

    template<typename OtherArenaTag>
    bool operator<=(const Str<OtherArenaTag>& other) const {
        return !(*this > other);
    }

    bool operator>=(const char *str) const { return !(*this < str); }
    bool operator>=(const Str &other) const { return !(*this < other); }

    template<typename OtherArenaTag>
    bool operator>=(const Str<OtherArenaTag>& other) const {
        return !(*this < other);
    }

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
    // Search Operations (with cross-arena support)
    // ========================================================================

    bool contains(const char *substr) const {
        if (!data || !substr) return false;
        return strstr(data, substr) != nullptr;
    }

    bool contains(const Str &substr) const {
        return contains(substr.c_str());
    }

    template<typename OtherArenaTag>
    bool contains(const Str<OtherArenaTag>& substr) const {
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

    template<typename OtherArenaTag>
    int find(const Str<OtherArenaTag>& substr) const {
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

    template<typename OtherArenaTag>
    bool starts_with(const Str<OtherArenaTag>& prefix) const {
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

    template<typename OtherArenaTag>
    bool ends_with(const Str<OtherArenaTag>& suffix) const {
        return ends_with(suffix.c_str());
    }

    // [Rest of the implementation continues with all the other methods from the original...]
    // Including: replace, numeric conversions, padding, format, utilities, etc.

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
};

// Global operators for char* on left side
template<typename ArenaTag>
bool operator==(const char* lhs, const Str<ArenaTag>& rhs) {
    return rhs == lhs;
}

template<typename ArenaTag>
bool operator!=(const char* lhs, const Str<ArenaTag>& rhs) {
    return rhs != lhs;
}

template<typename ArenaTag>
Str<ArenaTag> operator+(const char* lhs, const Str<ArenaTag>& rhs) {
    Str<ArenaTag> result(lhs);
    result += rhs;
    return result;
}
