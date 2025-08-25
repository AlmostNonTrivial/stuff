#include "os_layer.hpp"

// Comment out the next line to use memory filesystem instead of platform-specific
#define USE_PLATFORM_FS

#if defined(USE_PLATFORM_FS) && defined(_WIN32)
#include <windows.h>
#include <io.h>

#define OS_INVALID_HANDLE INVALID_HANDLE_VALUE

os_file_handle_t os_file_open(const char* filename, bool read_write, bool create) {
    DWORD access = read_write ? (GENERIC_READ | GENERIC_WRITE) : GENERIC_READ;
    DWORD creation = create ? OPEN_ALWAYS : OPEN_EXISTING;
    HANDLE handle = CreateFileA(filename, access, FILE_SHARE_READ | FILE_SHARE_WRITE,
                               NULL, creation, FILE_ATTRIBUTE_NORMAL, NULL);
    return (os_file_handle_t)handle;
}

void os_file_close(os_file_handle_t handle) {
    if (handle != OS_INVALID_HANDLE) {
        CloseHandle((HANDLE)handle);
    }
}

bool os_file_exists(const char* filename) {
    DWORD attrs = GetFileAttributesA(filename);
    return (attrs != INVALID_FILE_ATTRIBUTES);
}

void os_file_delete(const char* filename) {
    DeleteFileA(filename);
}

os_file_size_t os_file_read(os_file_handle_t handle, void* buffer, os_file_size_t size) {
    DWORD bytes_read = 0;
    ReadFile((HANDLE)handle, buffer, (DWORD)size, &bytes_read, NULL);
    return (os_file_size_t)bytes_read;
}

os_file_size_t os_file_write(os_file_handle_t handle, const void* buffer, os_file_size_t size) {
    DWORD bytes_written = 0;
    WriteFile((HANDLE)handle, buffer, (DWORD)size, &bytes_written, NULL);
    return (os_file_size_t)bytes_written;
}

void os_file_sync(os_file_handle_t handle) {
    FlushFileBuffers((HANDLE)handle);
}

void os_file_seek(os_file_handle_t handle, os_file_offset_t offset) {
    LARGE_INTEGER li;
    li.QuadPart = offset;
    SetFilePointerEx((HANDLE)handle, li, NULL, FILE_BEGIN);
}

os_file_offset_t os_file_size(os_file_handle_t handle) {
    LARGE_INTEGER size;
    GetFileSizeEx((HANDLE)handle, &size);
    return (os_file_offset_t)size.QuadPart;
}

void os_file_truncate(os_file_handle_t handle, os_file_offset_t size) {
    LARGE_INTEGER li;
    li.QuadPart = size;
    SetFilePointerEx((HANDLE)handle, li, NULL, FILE_BEGIN);
    SetEndOfFile((HANDLE)handle);
}

#elif defined(USE_PLATFORM_FS)
// Unix/Linux implementation
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

#define OS_INVALID_HANDLE -1

os_file_handle_t os_file_open(const char* filename, bool read_write, bool create) {
    int flags = read_write ? O_RDWR : O_RDONLY;
    if (create) flags |= O_CREAT;
    return open(filename, flags, 0644);
}

void os_file_close(os_file_handle_t handle) {
    if (handle != OS_INVALID_HANDLE) {
        close(handle);
    }
}

bool os_file_exists(const char* filename) {
    struct stat st;
    return stat(filename, &st) == 0;
}

void os_file_delete(const char* filename) {
    unlink(filename);
}

os_file_size_t os_file_read(os_file_handle_t handle, void* buffer, os_file_size_t size) {
    ssize_t result = read(handle, buffer, size);
    return (result < 0) ? 0 : (os_file_size_t)result;
}

os_file_size_t os_file_write(os_file_handle_t handle, const void* buffer, os_file_size_t size) {
    ssize_t result = write(handle, buffer, size);
    return (result < 0) ? 0 : (os_file_size_t)result;
}

void os_file_sync(os_file_handle_t handle) {
    fsync(handle);
}

void os_file_seek(os_file_handle_t handle, os_file_offset_t offset) {
    lseek(handle, offset, SEEK_SET);
}

os_file_offset_t os_file_size(os_file_handle_t handle) {
    struct stat st;
    if (fstat(handle, &st) == 0) {
        return (os_file_offset_t)st.st_size;
    }
    return 0;
}

void os_file_truncate(os_file_handle_t handle, os_file_offset_t size) {
    ftruncate(handle, size);
}

#else
// Memory filesystem implementation
#include <unordered_map>
#include <vector>
#include <string>
#include <cstring>
#include <algorithm>

#define OS_INVALID_HANDLE 0

struct MemoryFile {
    std::vector<uint8_t> data;
    size_t position;
    bool read_write;

    MemoryFile() : position(0), read_write(false) {}
};

struct MemoryFileSystem {
    std::unordered_map<std::string, MemoryFile> files;
    std::unordered_map<os_file_handle_t, std::string> open_handles;
    os_file_handle_t next_handle;

    MemoryFileSystem() : next_handle(1) {}
};

static MemoryFileSystem g_filesystem;

os_file_handle_t os_file_open(const char* filename, bool read_write, bool create) {
    std::string filepath(filename);

    // Check if file exists
    auto file_it = g_filesystem.files.find(filepath);

    if (file_it == g_filesystem.files.end()) {
        if (!create) {
            return OS_INVALID_HANDLE;
        }
        // Create new file
        g_filesystem.files[filepath] = MemoryFile();
        file_it = g_filesystem.files.find(filepath);
    }

    file_it->second.position = 0;
    file_it->second.read_write = read_write;

    os_file_handle_t handle = g_filesystem.next_handle++;
    g_filesystem.open_handles[handle] = filepath;

    return handle;
}

void os_file_close(os_file_handle_t handle) {
    if (handle != OS_INVALID_HANDLE) {
        g_filesystem.open_handles.erase(handle);
    }
}

bool os_file_exists(const char* filename) {
    std::string filepath(filename);
    return g_filesystem.files.find(filepath) != g_filesystem.files.end();
}

void os_file_delete(const char* filename) {
    std::string filepath(filename);
    g_filesystem.files.erase(filepath);

    // Close any open handles to this file
    for (auto it = g_filesystem.open_handles.begin(); it != g_filesystem.open_handles.end();) {
        if (it->second == filepath) {
            it = g_filesystem.open_handles.erase(it);
        } else {
            ++it;
        }
    }
}

static MemoryFile* get_file_from_handle(os_file_handle_t handle) {
    auto handle_it = g_filesystem.open_handles.find(handle);
    if (handle_it == g_filesystem.open_handles.end()) {
        return nullptr;
    }

    auto file_it = g_filesystem.files.find(handle_it->second);
    if (file_it == g_filesystem.files.end()) {
        return nullptr;
    }

    return &file_it->second;
}

os_file_size_t os_file_read(os_file_handle_t handle, void* buffer, os_file_size_t size) {
    MemoryFile* file = get_file_from_handle(handle);
    if (!file) return 0;

    os_file_size_t bytes_to_read = std::min(size, (os_file_size_t)(file->data.size() - file->position));

    if (bytes_to_read > 0) {
        memcpy(buffer, file->data.data() + file->position, bytes_to_read);
        file->position += bytes_to_read;
    }

    return bytes_to_read;
}

os_file_size_t os_file_write(os_file_handle_t handle, const void* buffer, os_file_size_t size) {
    MemoryFile* file = get_file_from_handle(handle);
    if (!file || !file->read_write) return 0;

    // Resize file if necessary
    size_t required_size = file->position + size;
    if (file->data.size() < required_size) {
        file->data.resize(required_size);
    }

    memcpy(file->data.data() + file->position, buffer, size);
    file->position += size;

    return size;
}

void os_file_sync(os_file_handle_t handle) {
    // No-op for memory-based implementation
    (void)handle;
}

void os_file_seek(os_file_handle_t handle, os_file_offset_t offset) {
    MemoryFile* file = get_file_from_handle(handle);
    if (!file) return;

    file->position = std::min((size_t)offset, file->data.size());
}

os_file_offset_t os_file_size(os_file_handle_t handle) {
    MemoryFile* file = get_file_from_handle(handle);
    if (!file) return 0;

    return (os_file_offset_t)file->data.size();
}

void os_file_truncate(os_file_handle_t handle, os_file_offset_t size) {
    MemoryFile* file = get_file_from_handle(handle);
    if (!file || !file->read_write) return;

    file->data.resize((size_t)size);
    if (file->position > (size_t)size) {
        file->position = (size_t)size;
    }
}

#endif
