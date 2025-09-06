/*
** 2024 SQL-FromScratch
**
** OS LAYER IMPLEMENTATION
**
** This file provides three different backend implementations of the OS layer
** API, selected at compile time via preprocessor directives.
**
** CONFIGURATION
**
** Define USE_PLATFORM_FS to use native filesystem APIs (default: memory filesystem)
** The memory filesystem is ideal for testing and debugging.
**
** IMPLEMENTATION NOTES
**
** Windows Backend:
**   - Uses Win32 API with explicit Unicode/ANSI selection (we use ANSI)
**   - Requires careful HANDLE casting and error checking
**   - File sharing is enabled to allow multiple readers
**
** Unix/Linux Backend:
**   - Uses POSIX system calls directly
**   - File descriptors are simple integers
**   - Permissions fixed at 0644 for new files
**
** Memory Filesystem:
**   - Files stored as dynamic byte arrays
**   - Handles are incrementing integers starting from 1
**   - Supports multiple handles to same file with independent positions
**   - Properly implements sparse file semantics (zero-fill on extend)
**
** ERROR HANDLING
**
** All implementations follow consistent error conventions:
**   - Invalid operations return 0, false, or OS_INVALID_HANDLE
**   - No exceptions are thrown
**   - Errors are silent (no stderr output)
*/

#include "os_layer.hpp"

// Comment out the next line to use memory filesystem instead of platform-specific
#define USE_PLATFORM_FS

#if defined(USE_PLATFORM_FS) && defined(_WIN32)
/*
** WINDOWS IMPLEMENTATION
**
** Uses Win32 API for file operations. Key considerations:
** - HANDLEs are kernel objects that must be closed
** - File positions are 64-bit (LARGE_INTEGER)
** - Sharing mode allows concurrent access
*/

#include <windows.h>
#include <io.h>

#define OS_INVALID_HANDLE INVALID_HANDLE_VALUE

os_file_handle_t
os_file_open(const char *filename, bool read_write, bool create)
{
	DWORD access = read_write ? (GENERIC_READ | GENERIC_WRITE) : GENERIC_READ;
	DWORD creation = create ? OPEN_ALWAYS : OPEN_EXISTING;

	/* Allow shared reading and writing for database concurrency */
	HANDLE handle =
		CreateFileA(filename, access, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, creation, FILE_ATTRIBUTE_NORMAL, NULL);

	return (os_file_handle_t)handle;
}

void
os_file_close(os_file_handle_t handle)
{
	if (handle != OS_INVALID_HANDLE)
	{
		CloseHandle((HANDLE)handle);
	}
}

bool
os_file_exists(const char *filename)
{
	DWORD attrs = GetFileAttributesA(filename);
	return (attrs != INVALID_FILE_ATTRIBUTES);
}

void
os_file_delete(const char *filename)
{
	DeleteFileA(filename);
}

os_file_size_t
os_file_read(os_file_handle_t handle, void *buffer, os_file_size_t size)
{
	DWORD bytes_read = 0;
	ReadFile((HANDLE)handle, buffer, (DWORD)size, &bytes_read, NULL);
	return (os_file_size_t)bytes_read;
}

os_file_size_t
os_file_write(os_file_handle_t handle, const void *buffer, os_file_size_t size)
{
	DWORD bytes_written = 0;
	WriteFile((HANDLE)handle, buffer, (DWORD)size, &bytes_written, NULL);
	return (os_file_size_t)bytes_written;
}

void
os_file_sync(os_file_handle_t handle)
{
	FlushFileBuffers((HANDLE)handle);
}

void
os_file_seek(os_file_handle_t handle, os_file_offset_t offset)
{
	LARGE_INTEGER li;
	li.QuadPart = offset;
	SetFilePointerEx((HANDLE)handle, li, NULL, FILE_BEGIN);
}

os_file_offset_t
os_file_size(os_file_handle_t handle)
{
	LARGE_INTEGER size;
	GetFileSizeEx((HANDLE)handle, &size);
	return (os_file_offset_t)size.QuadPart;
}

void
os_file_truncate(os_file_handle_t handle, os_file_offset_t size)
{
	LARGE_INTEGER li;
	li.QuadPart = size;
	SetFilePointerEx((HANDLE)handle, li, NULL, FILE_BEGIN);
	SetEndOfFile((HANDLE)handle);
}

#elif defined(USE_PLATFORM_FS)
/*
** UNIX/LINUX IMPLEMENTATION
**
** Uses POSIX system calls. Key considerations:
** - File descriptors are small integers
** - O_CREAT requires mode parameter (0644)
** - fsync() forces data to disk but is expensive
*/

#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

os_file_handle_t
os_file_open(const char *filename, bool read_write, bool create)
{
	int flags = read_write ? O_RDWR : O_RDONLY;
	if (create)
		flags |= O_CREAT;

	/* Mode 0644 = owner read/write, group/other read only */
	return open(filename, flags, 0644);
}

void
os_file_close(os_file_handle_t handle)
{
	if (handle != OS_INVALID_HANDLE)
	{
		close(handle);
	}
}

bool
os_file_exists(const char *filename)
{
	struct stat st;
	return stat(filename, &st) == 0;
}

void
os_file_delete(const char *filename)
{
	unlink(filename);
}

os_file_size_t
os_file_read(os_file_handle_t handle, void *buffer, os_file_size_t size)
{
	ssize_t result = read(handle, buffer, size);
	return (result < 0) ? 0 : (os_file_size_t)result;
}

os_file_size_t
os_file_write(os_file_handle_t handle, const void *buffer, os_file_size_t size)
{
	ssize_t result = write(handle, buffer, size);
	return (result < 0) ? 0 : (os_file_size_t)result;
}

void
os_file_sync(os_file_handle_t handle)
{
	fsync(handle);
}

void
os_file_seek(os_file_handle_t handle, os_file_offset_t offset)
{
	lseek(handle, offset, SEEK_SET);
}

os_file_offset_t
os_file_size(os_file_handle_t handle)
{
	struct stat st;
	if (fstat(handle, &st) == 0)
	{
		return (os_file_offset_t)st.st_size;
	}
	return 0;
}

void
os_file_truncate(os_file_handle_t handle, os_file_offset_t size)
{
	ftruncate(handle, size);
}

#else

/*
** MEMORY FILESYSTEM IMPLEMENTATION
**
** In-memory filesystem for testing and development using arena-based containers.
** Key features:
** - Files are byte arrays that grow dynamically
** - Multiple handles can reference the same file
** - Each handle maintains its own position
** - Implements proper sparse file semantics
**
** DESIGN NOTES
**
** File Storage:
**   Files are stored in a hash_map<string, array<uint8_t>> keyed by filepath.
**   Each file is represented as a dynamic byte array using the arena allocator.
**
** Handle Management:
**   Handles are integers that index into a hash_map. Each handle tracks:
**   - The filepath it references (as a string)
**   - Current read/write position
**   - Whether it was opened for writing
**
** Sparse File Support:
**   When writing past EOF, the intervening bytes are zero-filled to match
**   POSIX behavior. This ensures database pages are properly initialized.
**
** Memory Management:
**   Uses arena allocator for all dynamic memory. The arena is configured
**   for the global_arena tag.
*/

#include "arena.hpp"

/* Define invalid handle for memory filesystem */
#ifndef OS_INVALID_HANDLE
#define OS_INVALID_HANDLE ((os_file_handle_t)0)
#endif

/*
** FILE HANDLE STRUCTURE
**
** Represents an open handle to a file. Multiple handles can point to
** the same file with independent positions.
*/
struct file_handle
{
    string<global_arena> filepath;   /* Path to file */
    size_t               position;   /* Current read/write position */
    bool                 read_write; /* true if opened for writing */
};

/*
** MEMORY FILESYSTEM STATE
**
** Global state for the in-memory filesystem. Not thread-safe.
*/
struct memory_file_system
{
    /* Map from filepath to file contents */
    hash_map<string<global_arena>, array<uint8_t, global_arena>, global_arena> files;

    /* Map from handle ID to handle data */
    hash_map<os_file_handle_t, file_handle, global_arena> handles;

    /* Next handle ID to assign (starts at 1, 0 is invalid) */
    os_file_handle_t next_handle = 1;

    void init()
    {
        files.init();
        handles.init();
        next_handle = 1;
    }
};

static memory_file_system g_filesystem;

os_file_handle_t
os_file_open(const char *filename, bool read_write, bool create)
{
    if (!g_filesystem.files.entries)
    {
        g_filesystem.init();
    }

    /* Create string key for the file */
    string<global_arena> filepath_key = string<global_arena>::make(filename);

    /* Check if file exists */
    auto *file_data = g_filesystem.files.get(filepath_key);

    if (!file_data)
    {
        if (!create)
        {
            return OS_INVALID_HANDLE;
        }

        /* Create new file with empty byte array */
        array<uint8_t, global_arena> new_file;
        g_filesystem.files.insert(filepath_key, new_file);
        file_data = g_filesystem.files.get(filepath_key);
    }

    /* Create new handle with its own state */
    os_file_handle_t handle = g_filesystem.next_handle++;

    file_handle fh;
    fh.filepath = string<global_arena>::make(filename);
    fh.position = 0;
    fh.read_write = read_write;

    g_filesystem.handles.insert(handle, fh);

    return handle;
}

void
os_file_close(os_file_handle_t handle)
{
    if (handle != OS_INVALID_HANDLE)
    {
        g_filesystem.handles.remove(handle);
    }
}

bool
os_file_exists(const char *filename)
{
    if (!g_filesystem.files.entries)
    {
        return false;
    }

    string<global_arena> filepath_key = string<global_arena>::make(filename);
    return g_filesystem.files.contains(filepath_key);
}

void
os_file_delete(const char *filename)
{
    if (!g_filesystem.files.entries)
    {
        return;
    }

    string<global_arena> filepath_key = string<global_arena>::make(filename);

    /* Delete from the files map */
    g_filesystem.files.remove(filepath_key);

    /* Close any open handles to this file */
    /* Collect handles first to avoid modifying while iterating */
    array<os_file_handle_t, global_arena> handles_to_close;

    for (uint32_t i = 0; i < g_filesystem.handles.capacity; i++)
    {
        auto &entry = g_filesystem.handles.entries[i];
        if (entry.state == hash_map<os_file_handle_t, file_handle, global_arena>::Entry::OCCUPIED)
        {
            if (entry.value.filepath.equals(filename))
            {
                handles_to_close.push(entry.key);
            }
        }
    }

    /* Now close the collected handles */
    for (uint32_t i = 0; i < handles_to_close.size; i++)
    {
        os_file_close(handles_to_close.data[i]);
    }
}

os_file_size_t
os_file_read(os_file_handle_t handle, void *buffer, os_file_size_t size)
{
    auto *handle_data = g_filesystem.handles.get(handle);
    if (!handle_data)
    {
        return 0;
    }

    auto *file_data = g_filesystem.files.get(handle_data->filepath);
    if (!file_data)
    {
        return 0;
    }

    size_t &position = handle_data->position;
    os_file_size_t bytes_to_read = 0;

    if (position < file_data->size)
    {
        bytes_to_read = (os_file_size_t)(file_data->size - position);
        if (size < bytes_to_read)
        {
            bytes_to_read = size;
        }
    }

    if (bytes_to_read > 0)
    {
        memcpy(buffer, file_data->data + position, bytes_to_read);
        position += bytes_to_read;
    }

    return bytes_to_read;
}

os_file_size_t
os_file_write(os_file_handle_t handle, const void *buffer, os_file_size_t size)
{
    auto *handle_data = g_filesystem.handles.get(handle);
    if (!handle_data || !handle_data->read_write)
    {
        return 0;
    }

    auto *file_data = g_filesystem.files.get(handle_data->filepath);
    if (!file_data)
    {
        return 0;
    }

    size_t &position = handle_data->position;
    size_t required_size = position + size;

    /*
    ** SPARSE FILE HANDLING
    **
    ** If writing past EOF, we need to zero-fill the gap to match
    ** POSIX sparse file behavior. This is critical for database
    ** correctness as uninitialized pages must read as zeros.
    */
    if (position > file_data->size)
    {
        /* Writing past EOF - need to zero-fill the gap */
        file_data->reserve(required_size);

        /* Zero-fill from current size to write position */
        memset(file_data->data + file_data->size, 0, position - file_data->size);

        file_data->size = required_size;
    }
    else if (required_size > file_data->size)
    {
        /* Writing extends the file but no gap */
        file_data->reserve(required_size);
        file_data->size = required_size;
    }
    else
    {
        /* Writing within existing file bounds */
        file_data->reserve(required_size);
    }

    /* Perform the write */
    memcpy(file_data->data + position, buffer, size);
    position += size;

    return size;
}

void
os_file_sync(os_file_handle_t handle)
{
    /* No-op for memory-based implementation */
    (void)handle;
}

void
os_file_seek(os_file_handle_t handle, os_file_offset_t offset)
{
    auto *handle_data = g_filesystem.handles.get(handle);
    if (!handle_data)
    {
        return;
    }

    /* Allow seeking beyond EOF to support sparse files */
    handle_data->position = (size_t)offset;
}

os_file_offset_t
os_file_size(os_file_handle_t handle)
{
    auto *handle_data = g_filesystem.handles.get(handle);
    if (!handle_data)
    {
        return 0;
    }

    auto *file_data = g_filesystem.files.get(handle_data->filepath);
    if (!file_data)
    {
        return 0;
    }

    return (os_file_offset_t)file_data->size;
}

void
os_file_truncate(os_file_handle_t handle, os_file_offset_t size)
{
    auto *handle_data = g_filesystem.handles.get(handle);
    if (!handle_data || !handle_data->read_write)
    {
        return;
    }

    auto *file_data = g_filesystem.files.get(handle_data->filepath);
    if (!file_data)
    {
        return;
    }

    /* Resize the file */
    if ((size_t)size > file_data->size)
    {
        /* Extending - need to zero-fill */
        file_data->reserve((size_t)size);
        /* Zero-fill the new bytes */
        memset(file_data->data + file_data->size, 0, (size_t)size - file_data->size);
    }

    file_data->size = (size_t)size;

    /* Adjust position if it's beyond new size */
    if (handle_data->position > (size_t)size)
    {
        handle_data->position = (size_t)size;
    }
}
#endif
