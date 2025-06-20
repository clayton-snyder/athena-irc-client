#include "stringutils.h"

#include <assert.h>
#include <ctype.h>
#include <string.h>

bool strut_startswith(const char* str, const char* prefix) {
    assert(str != NULL);
    assert(prefix != NULL);
    return strncmp(str, prefix, strlen(prefix)) == 0;
}

bool strut_startswithi(const char* str, const char* prefix) {
    assert(str != NULL);
    assert(prefix != NULL);
    return strut_strncmpi(str, prefix, strlen(prefix)) == 0;
}

int strut_strncmpi(const char *const a, const char *const b, size_t count) {
#ifdef _WIN32
    return _strnicmp(a, b, count);
#else
    return strncasecmp(a, b, count);
#endif
}

int strut_strcmpi(const char *const a, const char *const b) {
#ifdef _WIN32
    return _stricmp(a, b);
#else
    return strcasecmp(a, b);
#endif

}

int strut_ctoi(char c) {
    int result = c - '0';
    if (result < 0 || result > 9) return -1;
    return result;
}
