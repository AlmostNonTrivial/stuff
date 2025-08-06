#include "os_layer.hpp"

#ifdef _WIN32
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

#else
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

#endif
