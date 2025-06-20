#pragma once

#include <stdbool.h>
#include <string.h>

bool strut_startswith(const char* str, const char* prefix);
bool strut_startswithi(const char* str, const char* prefix);

int strut_strncmpi(const char *const a, const char *const b, size_t count);
int strut_strcmpi(const char *const a, const char *const b);

// Returns -1 if the character is out of the range of '0' - '9'
int strut_ctoi(char c);

