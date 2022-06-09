#include <climits>
#include <cstdio>
#include <string>

#include "Common/Data/Text/Parsers.h"

#include "Common/StringUtils.h"

bool Version::ParseVersionString(std::string str) {
	if (str.empty())
		return false;
	if (str[0] == 'v')
		str = str.substr(1);
	if (3 != sscanf(str.c_str(), "%i.%i.%i", &major, &minor, &sub)) {
		sub = 0;
		if (2 != sscanf(str.c_str(), "%i.%i", &major, &minor))
			return false;
	}
	return true;
}

std::string Version::ToString() const {
	char temp[128];
	sprintf(temp, "%i.%i.%i", major, minor, sub);
	return std::string(temp);
}

int Version::ToInteger() const {
	// This allows for ~2000 major versions, ~100 minor versions, and ~10000 sub versions.
	return major * 1000000 + minor * 10000 + sub;
}

bool ParseMacAddress(std::string str, uint8_t macAddr[6]) {
	unsigned int mac[6];
	if (6 != sscanf(str.c_str(), "%02x:%02x:%02x:%02x:%02x:%02x", &mac[0], &mac[1], &mac[2], &mac[3], &mac[4], &mac[5])) {
		return false;
	}
	for (int i = 0; i < 6; i++) {
		macAddr[i] = mac[i];
	}
	return true;
}

bool TryParse(const std::string &str, uint32_t *const output) {
	char *endptr = NULL;

	// Holy crap this is ugly.

	// Reset errno to a value other than ERANGE
	errno = 0;

	unsigned long value = strtoul(str.c_str(), &endptr, 0);

	if (!endptr || *endptr)
		return false;

	if (errno == ERANGE)
		return false;

	if (ULONG_MAX > UINT_MAX) {
#ifdef _MSC_VER
#pragma warning (disable:4309)
#endif
		// Note: The typecasts avoid GCC warnings when long is 32 bits wide.
		if (value >= static_cast<unsigned long>(0x100000000ull)
			&& value <= static_cast<unsigned long>(0xFFFFFFFF00000000ull))
			return false;
	}

	*output = static_cast<uint32_t>(value);
	return true;
}

bool TryParse(const std::string &str, bool *const output) {
	if ("1" == str || !strcasecmp("true", str.c_str()))
		*output = true;
	else if ("0" == str || !strcasecmp("false", str.c_str()))
		*output = false;
	else
		return false;

	return true;
}
