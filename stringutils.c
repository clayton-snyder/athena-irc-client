#include "stringutils.h"

#include <assert.h>

bool strutils_startswith(const char* str, const char* prefix) {
    assert(str != NULL);
    assert(prefix != NULL);
    return strncmp(str, prefix, strlen(prefix)) == 0;
}

