/*
** 2024 SQL-FromScratch
**
** Comment out the following to use an in-memory fs
*/

#define USE_PLATFORM_FS

#include "os_layer.hpp"

#if defined(USE_PLATFORM_FS) && defined(_WIN32)

#include <windows.h>
#include <io.h>

#define OS_INVALID_HANDLE INVALID_HANDLE_VALUE

os_file_handle_t
os_file_open(const char *filename, bool read_write, bool create)
{
	DWORD access = read_write ? (GENERIC_READ | GENERIC_WRITE) : GENERIC_READ;
	DWORD creation = create ? OPEN_ALWAYS : OPEN_EXISTING;

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

#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

os_file_handle_t
os_file_open(const char *filename, bool read_write, bool create)
{
	int flags = read_write ? O_RDWR : O_RDONLY;
	if (create)
		flags |= O_CREAT;

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

#include "arena.hpp"
#include "containers.hpp"
#include <cstring>

#ifndef OS_INVALID_HANDLE
#define OS_INVALID_HANDLE ((os_file_handle_t)0)
#endif

struct file_handle
{
	const char *filepath;
	size_t		position;
	bool		read_write;
};

struct memory_file_system
{

	hash_map<std::string_view, array<uint8_t>> files;

	hash_map<os_file_handle_t, file_handle> handles;

	os_file_handle_t next_handle = 1;

	void
	init()
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
	if (!g_filesystem.files.capacity())
	{
		g_filesystem.init();
	}

	std::string_view filepath_key = arena_intern<global_arena>(filename);

	auto *file_data = g_filesystem.files.get(filepath_key);

	if (!file_data)
	{
		if (!create)
		{
			return OS_INVALID_HANDLE;
		}

		array<uint8_t, global_arena> new_file;
		g_filesystem.files.insert(filepath_key, new_file);
		file_data = g_filesystem.files.get(filepath_key);
	}

	os_file_handle_t handle = g_filesystem.next_handle++;

	file_handle fh;
	fh.filepath = filepath_key.data();
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
	if (!g_filesystem.files.capacity())
	{
		return false;
	}

	std::string_view filepath_key = arena_intern<global_arena>(filename);
	return g_filesystem.files.contains(filepath_key);
}

void
os_file_delete(const char *filename)
{
	if (!g_filesystem.files.capacity())
	{
		return;
	}

	std::string_view filepath_key = arena_intern<global_arena>(filename);

	g_filesystem.files.remove(filepath_key);

	array<os_file_handle_t, global_arena> handles_to_close;

	for (auto it = g_filesystem.handles.begin(); it != g_filesystem.handles.end(); ++it)
	{
		auto [handle_id, handle_data] = *it;
		if (strcmp(handle_data.filepath, filename) == 0)
		{
			handles_to_close.push(handle_id);
		}
	}

	for (uint32_t i = 0; i < handles_to_close.size(); i++)
	{
		os_file_close(handles_to_close[i]);
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

	std::string_view filepath_key(handle_data->filepath);
	auto			*file_data = g_filesystem.files.get(filepath_key);
	if (!file_data)
	{
		return 0;
	}

	size_t		  &position = handle_data->position;
	os_file_size_t bytes_to_read = 0;

	if (position < file_data->size())
	{
		bytes_to_read = (os_file_size_t)(file_data->size() - position);
		if (size < bytes_to_read)
		{
			bytes_to_read = size;
		}
	}

	if (bytes_to_read > 0)
	{
		memcpy(buffer, file_data->data() + position, bytes_to_read);
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

	std::string_view filepath_key(handle_data->filepath);
	auto			*file_data = g_filesystem.files.get(filepath_key);
	if (!file_data)
	{
		return 0;
	}

	size_t &position = handle_data->position;
	size_t	required_size = position + size;

	if (required_size > file_data->size())
	{
		size_t old_size = file_data->size();
		file_data->resize(required_size);

		if (position > old_size)
		{
			memset(file_data->data() + old_size, 0, position - old_size);
		}
	}

	memcpy(file_data->data() + position, buffer, size);
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

	std::string_view filepath_key(handle_data->filepath);
	auto			*file_data = g_filesystem.files.get(filepath_key);
	if (!file_data)
	{
		return 0;
	}

	return (os_file_offset_t)file_data->size();
}

void
os_file_truncate(os_file_handle_t handle, os_file_offset_t size)
{
	auto *handle_data = g_filesystem.handles.get(handle);
	if (!handle_data || !handle_data->read_write)
	{
		return;
	}

	std::string_view filepath_key(handle_data->filepath);
	auto			*file_data = g_filesystem.files.get(filepath_key);
	if (!file_data)
	{
		return;
	}

	size_t new_size = (size_t)size;
	size_t old_size = file_data->size();

	if (new_size != old_size)
	{
		file_data->resize(new_size);

		if (new_size > old_size)
		{
			memset(file_data->data() + old_size, 0, new_size - old_size);
		}
	}

	if (handle_data->position > new_size)
	{
		handle_data->position = new_size;
	}
}
#endif
