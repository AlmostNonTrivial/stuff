/*
** 2024 SQL-FromScratch
**
** OVERVIEW
**
** The OS layer provides a portable filesystem abstraction that enables the
** SQL engine to operate across different platforms and environments. It
** abstracts file operations behind a simple, handle-based API that can be
** backed by native filesystem calls or an in-memory implementation for testing.
**
** KEY CONCEPTS
**
** Handles: All file operations use opaque handles rather than raw file
** descriptors or pointers. This provides a clean abstraction boundary and
** enables handle validation in debug builds.
**
** Portability: The same API works across Windows, Unix/Linux, and in-memory
** filesystems. Platform differences are hidden behind the abstraction.
**
** Memory Filesystem: For testing and development, an in-memory filesystem
** implementation allows the database to run without touching disk. This is
** invaluable for unit tests and debugging.
**
** DESIGN DECISIONS
**
** Fixed API: The API is intentionally minimal, supporting only operations
** needed by the pager. No directory operations, permissions, or metadata
** beyond file size.
**
** Synchronous Only: All operations are synchronous/blocking. Async I/O would
** complicate the pager significantly with minimal benefit for an educational
** database.
**
** Binary Mode: All files are treated as binary. The database works with
** fixed-size pages and has no need for text mode translations.
**
** IMPLEMENTATION STRATEGIES
**
** The layer supports three backends selected at compile time:
**
** 1. Windows Native: Uses Win32 API (CreateFile, ReadFile, etc.)
**    - Provides optimal performance on Windows
**    - Handles are HANDLE types cast to os_file_handle_t
**
** 2. Unix/Linux Native: Uses POSIX API (open, read, write, etc.)
**    - Provides optimal performance on Unix-like systems
**    - Handles are file descriptors cast to os_file_handle_t
**
** 3. Memory Filesystem: Pure in-memory implementation
**    - Enables testing without disk I/O
**    - Files are stored as byte arrays in hash maps
**    - Handles are incrementing integers
**
** THREAD SAFETY
**
** The OS layer is NOT thread-safe. The SQL engine must ensure that:
** - File operations are serialized per handle
** - The memory filesystem is accessed by only one thread
**
** This is acceptable for an educational database focused on clarity over
** concurrent performance.
*/


#pragma once

#include <cstddef>
#include <stdint.h>

/*
** TYPE DEFINITIONS
**
** These types provide consistent sizes across platforms while maintaining
** enough range for practical database files (up to 2^63 bytes).
*/
typedef int64_t os_file_offset_t;  /* File position/size in bytes */
typedef size_t  os_file_size_t;    /* Read/write operation sizes */

/*
** HANDLE DEFINITION
**
** Opaque handle type that varies by platform. The actual type is hidden
** to prevent direct manipulation by higher layers.
*/
#ifdef _WIN32
typedef void *os_file_handle_t;
#define OS_INVALID_HANDLE ((os_file_handle_t)INVALID_HANDLE_VALUE)
#else
typedef int os_file_handle_t;
#define OS_INVALID_HANDLE ((os_file_handle_t)-1)
#endif

/*
** FILE OPERATIONS
**
** Core operations needed by the pager. Each operation has clear failure
** semantics - either returning false, 0, or OS_INVALID_HANDLE on error.
*/

/*
** Open or create a file.
**
** Parameters:
**   filename   - Path to the file (UTF-8 on all platforms)
**   read_write - true for read/write access, false for read-only
**   create     - true to create file if it doesn't exist
**
** Returns:
**   Valid handle on success, OS_INVALID_HANDLE on failure
**
** Notes:
**   - Files are always opened in binary mode
**   - Created files have permissions 0644 on Unix
**   - Multiple handles to the same file are allowed
*/
os_file_handle_t os_file_open(const char *filename, bool read_write, bool create);

/*
** Close a file handle.
**
** Parameters:
**   handle - File handle to close
**
** Notes:
**   - Safe to call with OS_INVALID_HANDLE (no-op)
**   - After closing, handle should not be reused
**   - Does NOT sync/flush before closing
*/
void os_file_close(os_file_handle_t handle);

/*
** Check if a file exists.
**
** Parameters:
**   filename - Path to check
**
** Returns:
**   true if file exists, false otherwise
**
** Notes:
**   - Does not check permissions or file type
**   - Symbolic links are followed
*/
bool os_file_exists(const char *filename);

/*
** Delete a file from the filesystem.
**
** Parameters:
**   filename - Path to delete
**
** Notes:
**   - No error if file doesn't exist
**   - Memory filesystem closes all open handles to the file
**   - Native implementations may fail if file is open
*/
void os_file_delete(const char *filename);

/*
** Read data from file at current position.
**
** Parameters:
**   handle - File handle
**   buffer - Destination buffer (must be at least 'size' bytes)
**   size   - Maximum bytes to read
**
** Returns:
**   Number of bytes actually read (may be less than size at EOF)
**
** Notes:
**   - Advances file position by bytes read
**   - Returns 0 at EOF or on error
**   - Partial reads are possible near EOF
*/
os_file_size_t os_file_read(os_file_handle_t handle, void *buffer, os_file_size_t size);

/*
** Write data to file at current position.
**
** Parameters:
**   handle - File handle (must be opened with read_write=true)
**   buffer - Source buffer
**   size   - Bytes to write
**
** Returns:
**   Number of bytes actually written
**
** Notes:
**   - Advances file position by bytes written
**   - Extends file if writing past EOF
**   - Returns 0 if handle is read-only or on error
**   - When seeking past EOF then writing, intervening bytes are zero-filled
*/
os_file_size_t os_file_write(os_file_handle_t handle, const void *buffer, os_file_size_t size);

/*
** Flush buffered writes to stable storage.
**
** Parameters:
**   handle - File handle
**
** Notes:
**   - Ensures durability for committed transactions
**   - May be expensive on physical media
**   - No-op for memory filesystem
*/
void os_file_sync(os_file_handle_t handle);

/*
** Set file position for next read/write.
**
** Parameters:
**   handle - File handle
**   offset - Absolute byte offset from start of file
**
** Notes:
**   - Can seek past EOF (sparse file behavior)
**   - Seeking past EOF doesn't extend file until write
**   - Negative offsets are undefined behavior
*/
void os_file_seek(os_file_handle_t handle, os_file_offset_t offset);

/*
** Get current file size in bytes.
**
** Parameters:
**   handle - File handle
**
** Returns:
**   File size in bytes, or 0 on error
**
** Notes:
**   - Size reflects all writes, even if not synced
**   - For memory filesystem, returns logical size not capacity
*/
os_file_offset_t os_file_size(os_file_handle_t handle);

/*
** Truncate or extend file to specified size.
**
** Parameters:
**   handle - File handle (must be opened with read_write=true)
**   size   - New file size in bytes
**
** Notes:
**   - If size < current, data is lost
**   - If size > current, file is zero-extended
**   - File position is adjusted if beyond new size
**   - No-op if handle is read-only
*/
void os_file_truncate(os_file_handle_t handle, os_file_offset_t size);

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#endif

// Cross-platform virtual memory operations
struct VirtualMemory
{
	static void *
	reserve(size_t size)
	{
#ifdef _WIN32
		return VirtualAlloc(nullptr, size, MEM_RESERVE, PAGE_READWRITE);
#else
		void *ptr = mmap(nullptr, size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
		return (ptr == MAP_FAILED) ? nullptr : ptr;
#endif
	}

	static bool
	commit(void *addr, size_t size)
	{
#ifdef _WIN32
		return VirtualAlloc(addr, size, MEM_COMMIT, PAGE_READWRITE) != nullptr;
#else
		return mprotect(addr, size, PROT_READ | PROT_WRITE) == 0;
#endif
	}

	static void
	decommit(void *addr, size_t size)
	{
#ifdef _WIN32
		VirtualFree(addr, size, MEM_DECOMMIT);
#else
		madvise(addr, size, MADV_DONTNEED);
		mprotect(addr, size, PROT_NONE);
#endif
	}

	static void
	release(void *addr, size_t size)
	{
#ifdef _WIN32
		(void)size; // Windows doesn't need size for MEM_RELEASE
		VirtualFree(addr, 0, MEM_RELEASE);
#else
		munmap(addr, size);
#endif
	}

	static size_t
	page_size()
	{
		static size_t cached_size = 0;
		if (cached_size == 0)
		{
#ifdef _WIN32
			SYSTEM_INFO si;
			GetSystemInfo(&si);
			cached_size = si.dwPageSize;
#else
			cached_size = sysconf(_SC_PAGESIZE);
#endif
		}
		return cached_size;
	}

	static size_t
	round_to_pages(size_t size)
	{
		size_t page_sz = page_size();
		return ((size + page_sz - 1) / page_sz) * page_sz;
	}
};
