#include "ppsspp_config.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <direct.h>
#else
#include <strings.h>
#include <dirent.h>
#include <unistd.h>
#include <errno.h>
#endif
#include <cstring>
#include <string>
#include <set>
#include <algorithm>
#include <cstdio>
#include <sys/stat.h>
#include <ctype.h>

#include "Common/Data/Encoding/Utf8.h"
#include "Common/StringUtils.h"
#include "Common/File/DirListing.h"

#if !defined(__linux__) && !defined(_WIN32) && !defined(__QNX__)
#define stat64 stat
#endif

#ifdef HAVE_LIBNX
// Far from optimal, but I guess it works...
#define fseeko fseek
#define ftello ftell
#define fileno
#endif // HAVE_LIBNX

// Returns true if filename is a directory
bool isDirectory(const std::string & filename) {
	FileInfo info;
	getFileInfo(filename.c_str(), &info);
	return info.isDirectory;
}

bool getFileInfo(const char *path, FileInfo * fileInfo) {
	// TODO: Expand relative paths?
	fileInfo->fullName = path;

#ifdef _WIN32
	WIN32_FILE_ATTRIBUTE_DATA attrs;
	if (!GetFileAttributesExW(ConvertUTF8ToWString(path).c_str(), GetFileExInfoStandard, &attrs)) {
		fileInfo->size = 0;
		fileInfo->isDirectory = false;
		fileInfo->exists = false;
		return false;
	}
	fileInfo->size = (uint64_t)attrs.nFileSizeLow | ((uint64_t)attrs.nFileSizeHigh << 32);
	fileInfo->isDirectory = (attrs.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
	fileInfo->isWritable = (attrs.dwFileAttributes & FILE_ATTRIBUTE_READONLY) == 0;
	fileInfo->exists = true;
#else

	std::string copy(path);

#if (defined __ANDROID__) && (__ANDROID_API__ < 21)
	struct stat file_info;
	int result = stat(copy.c_str(), &file_info);
#else
	struct stat64 file_info;
	int result = stat64(copy.c_str(), &file_info);
#endif
	if (result < 0) {
		fileInfo->exists = false;
		return false;
	}

	fileInfo->isDirectory = S_ISDIR(file_info.st_mode);
	fileInfo->isWritable = false;
	fileInfo->size = file_info.st_size;
	fileInfo->exists = true;
	// HACK: approximation
	if (file_info.st_mode & 0200)
		fileInfo->isWritable = true;
#endif
	return true;
}

std::string getFileExtension(const std::string & fn) {
	int pos = (int)fn.rfind(".");
	if (pos < 0) return "";
	std::string ext = fn.substr(pos + 1);
	for (size_t i = 0; i < ext.size(); i++) {
		ext[i] = tolower(ext[i]);
	}
	return ext;
}

bool FileInfo::operator <(const FileInfo & other) const {
	if (isDirectory && !other.isDirectory)
		return true;
	else if (!isDirectory && other.isDirectory)
		return false;
	if (strcasecmp(name.c_str(), other.name.c_str()) < 0)
		return true;
	else
		return false;
}

size_t getFilesInDir(const char *directory, std::vector<FileInfo> * files, const char *filter, int flags) {
	size_t foundEntries = 0;
	std::set<std::string> filters;
	if (filter) {
		std::string tmp;
		while (*filter) {
			if (*filter == ':') {
				filters.insert(std::move(tmp));
			} else {
				tmp.push_back(*filter);
			}
			filter++;
		}
		if (!tmp.empty())
			filters.insert(std::move(tmp));
	}
#ifdef _WIN32
	// Find the first file in the directory.
	WIN32_FIND_DATA ffd;
	HANDLE hFind = FindFirstFileEx((ConvertUTF8ToWString(directory) + L"\\*").c_str(), FindExInfoStandard, &ffd, FindExSearchNameMatch, NULL, 0);
	if (hFind == INVALID_HANDLE_VALUE) {
		return 0;
	}
	// windows loop
	do
	{
		const std::string virtualName = ConvertWStringToUTF8(ffd.cFileName);
#else
	struct dirent *result = NULL;

	//std::string directoryWithSlash = directory;
	//if (directoryWithSlash.back() != '/')
	//	directoryWithSlash += "/";

	DIR *dirp = opendir(directory);
	if (!dirp)
		return 0;
	// non windows loop
	while ((result = readdir(dirp)))
	{
		const std::string virtualName(result->d_name);
#endif
		// check for "." and ".."
		if (virtualName == "." || virtualName == "..")
			continue;

		// Remove dotfiles (optional with flag.)
		if (!(flags & GETFILES_GETHIDDEN))
		{
#ifdef _WIN32
			if ((ffd.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN) != 0)
				continue;
#else
			if (virtualName[0] == '.')
				continue;
#endif
		}

		FileInfo info;
		info.name = virtualName;
		std::string dir = directory;

		// Only append a slash if there isn't one on the end.
		size_t lastSlash = dir.find_last_of("/");
		if (lastSlash != (dir.length() - 1))
			dir.append("/");

		info.fullName = dir + virtualName;
		info.isDirectory = isDirectory(info.fullName);
		info.exists = true;
		info.size = 0;
		info.isWritable = false;  // TODO - implement some kind of check
		if (!info.isDirectory) {
			std::string ext = getFileExtension(info.fullName);
			if (filter) {
				if (filters.find(ext) == filters.end())
					continue;
			}
		}

		if (files)
			files->push_back(std::move(info));
		foundEntries++;
#ifdef _WIN32
	} while (FindNextFile(hFind, &ffd) != 0);
	FindClose(hFind);
#else
	}
	closedir(dirp);
#endif
	if (files)
		std::sort(files->begin(), files->end());
	return foundEntries;
}

int64_t getDirectoryRecursiveSize(const std::string & path, const char *filter, int flags) {
	std::vector<FileInfo> fileInfo;
	getFilesInDir(path.c_str(), &fileInfo, filter, flags);
	int64_t sizeSum = 0;
	// Note: getFileInDir does not fill in fileSize properly.
	for (size_t i = 0; i < fileInfo.size(); i++) {
		FileInfo finfo;
		getFileInfo(fileInfo[i].fullName.c_str(), &finfo);
		if (!finfo.isDirectory)
			sizeSum += finfo.size;
		else
			sizeSum += getDirectoryRecursiveSize(finfo.fullName, filter, flags);
	}
	return sizeSum;
}

#ifdef _WIN32
// Returns a vector with the device names
std::vector<std::string> getWindowsDrives()
{
#if PPSSPP_PLATFORM(UWP)
	return std::vector<std::string>();  // TODO UWP http://stackoverflow.com/questions/37404405/how-to-get-logical-drives-names-in-windows-10
#else
	std::vector<std::string> drives;

	const DWORD buffsize = GetLogicalDriveStrings(0, NULL);
	std::vector<wchar_t> buff(buffsize);
	if (GetLogicalDriveStrings(buffsize, buff.data()) == buffsize - 1)
	{
		auto drive = buff.data();
		while (*drive)
		{
			std::string str(ConvertWStringToUTF8(drive));
			str.pop_back();	// we don't want the final backslash
			str += "/";
			drives.push_back(str);

			// advance to next drive
			while (*drive++) {}
		}
	}
	return drives;
#endif
}
#endif
