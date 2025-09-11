/*
** 2024 SQL-FromScratch
**
** The OS layer provides a portable filesystem abstraction for cross-plaform I/O
** and in-memory fs implementation for testing
**
*/


#pragma once

#include <cstddef>
#include <stdint.h>

typedef int64_t os_file_offset_t;
typedef size_t  os_file_size_t;

#ifdef _WIN32
typedef void *os_file_handle_t;
#define OS_INVALID_HANDLE ((os_file_handle_t)INVALID_HANDLE_VALUE)
#else
typedef int os_file_handle_t;
#define OS_INVALID_HANDLE ((os_file_handle_t)-1)
#endif


os_file_handle_t os_file_open(const char *filename, bool read_write, bool create);

void os_file_close(os_file_handle_t handle);

bool os_file_exists(const char *filename);

void os_file_delete(const char *filename);

os_file_size_t os_file_read(os_file_handle_t handle, void *buffer, os_file_size_t size);

os_file_size_t os_file_write(os_file_handle_t handle, const void *buffer, os_file_size_t size);

void os_file_sync(os_file_handle_t handle);

void os_file_seek(os_file_handle_t handle, os_file_offset_t offset);

os_file_offset_t os_file_size(os_file_handle_t handle);

void os_file_truncate(os_file_handle_t handle, os_file_offset_t size);

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#endif

// Cross-platform virtual memory operations for the custom allocators
struct virtual_memory
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
		(void)size;
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
