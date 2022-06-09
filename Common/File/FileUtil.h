// Copyright (C) 2003 Dolphin Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official SVN repository and contact information can be found at
// http://code.google.com/p/dolphin-emu/

#pragma once

#include <fstream>
#include <cstdio>
#include <string>
#include <vector>
#include <time.h>
#include <cstdint>

#include "Common/Common.h"

#ifdef _MSC_VER
inline struct tm* localtime_r(const time_t *clock, struct tm *result) {
	if (localtime_s(result, clock) == 0)
		return result;
	return NULL;
}
#endif

namespace File {

struct FileDetails {
	bool isDirectory;
	uint64_t size;
	uint64_t atime;
	uint64_t mtime;
	uint64_t ctime;
	uint32_t access;  // st_mode & 0x1ff
};

// Mostly to handle utf-8 filenames better on Windows.
FILE *OpenCFile(const std::string &filename, const char *mode);
bool OpenCPPFile(std::fstream & stream, const std::string &filename, std::ios::openmode mode);

// Resolves symlinks and similar.
std::string ResolvePath(const std::string &path);

// Returns true if file filename exists
bool Exists(const std::string &filename);

// Returns true if filename is a directory
bool IsDirectory(const std::string &filename);

// Returns file attributes.
bool GetFileDetails(const std::string &filename, FileDetails *details);

// Extracts the directory from a path.
std::string GetDir(const std::string &path);

// Extracts the filename from a path.
std::string GetFilename(std::string path);

// Returns struct with modification date of file
bool GetModifTime(const std::string &filename, tm &return_time);

// Returns the size of filename (64bit)
uint64_t GetFileSize(const std::string &filename);

// Overloaded GetSize, accepts FILE*
uint64_t GetFileSize(FILE *f);

// Returns true if successful, or path already exists.
bool CreateDir(const std::string &filename);

// Creates the full path of fullPath returns true on success
bool CreateFullPath(const std::string &fullPath);

// Deletes a given filename, return true on success
// Doesn't supports deleting a directory
bool Delete(const std::string &filename);

// Deletes a directory filename, returns true on success
bool DeleteDir(const std::string &filename);

// renames file srcFilename to destFilename, returns true on success 
bool Rename(const std::string &srcFilename, const std::string &destFilename);

// copies file srcFilename to destFilename, returns true on success 
bool Copy(const std::string &srcFilename, const std::string &destFilename);

// creates an empty file filename, returns true on success 
bool CreateEmptyFile(const std::string &filename);

// deletes the given directory and anything under it. Returns true on success.
bool DeleteDirRecursively(const std::string &directory);

// Returns the current directory
std::string GetCurrentDir();

// Create directory and copy contents (does not overwrite existing files)
void CopyDir(const std::string &source_path, const std::string &dest_path);

// Opens ini file (cheats, texture replacements etc.)
void openIniFile(const std::string& fileName);

// Set the current directory to given directory
bool SetCurrentDir(const std::string &directory);

const std::string &GetExeDirectory();


// simple wrapper for cstdlib file functions to
// hopefully will make error checking easier
// and make forgetting an fclose() harder
class IOFile {
public:
	IOFile();
	IOFile(FILE* file);
	IOFile(const std::string& filename, const char openmode[]);
	~IOFile();

	// Prevent copies.
	IOFile(const IOFile &) = delete;
	void operator=(const IOFile &) = delete;

	bool Open(const std::string& filename, const char openmode[]);
	bool Close();

	template <typename T>
	bool ReadArray(T* data, size_t length)
	{
		if (!IsOpen() || length != std::fread(data, sizeof(T), length, m_file))
			m_good = false;

		return m_good;
	}

	template <typename T>
	bool WriteArray(const T* data, size_t length)
	{
		if (!IsOpen() || length != std::fwrite(data, sizeof(T), length, m_file))
			m_good = false;

		return m_good;
	}

	bool ReadBytes(void* data, size_t length)
	{
		return ReadArray(reinterpret_cast<char*>(data), length);
	}

	bool WriteBytes(const void* data, size_t length)
	{
		return WriteArray(reinterpret_cast<const char*>(data), length);
	}

	bool IsOpen() { return NULL != m_file; }

	// m_good is set to false when a read, write or other function fails
	bool IsGood() {	return m_good; }
	operator void*() { return m_good ? m_file : NULL; }

	std::FILE* ReleaseHandle();

	std::FILE* GetHandle() { return m_file; }

	void SetHandle(std::FILE* file);

	bool Seek(int64_t off, int origin);
	uint64_t Tell();
	uint64_t GetSize();
	bool Resize(uint64_t size);
	bool Flush();

	// clear error state
	void Clear() {
		m_good = true;
#undef clearerr
		std::clearerr(m_file);
	}

private:
	std::FILE *m_file;
	bool m_good;
};

}  // namespace

// TODO: Refactor, this was moved from the old file_util.cpp.

// Whole-file reading/writing
bool writeStringToFile(bool text_file, const std::string &str, const char *filename);
bool writeDataToFile(bool text_file, const void* data, const unsigned int size, const char *filename);

bool readFileToString(bool text_file, const char *filename, std::string &str);
// Return value must be delete[]-d.
uint8_t *ReadLocalFile(const char *filename, size_t *size);
