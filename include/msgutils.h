#pragma once

#include "athena_types.h"
#include "msgqueue.h"

#include <stdbool.h>

typedef struct ircmsg {
    char *source;
    char *command;
    msglist params;
} ircmsg;

typedef enum timestamp_format {
    TIMESTAMP_FORMAT_YEAR_MONTH_DAY_TIME = 1,
    TIMESTAMP_FORMAT_MONTH_DAY_TIME = 2, 
    TIMESTAMP_FORMAT_TIME_ONLY = 3,
    TIMESTAMP_FORMAT_YEAR_MONTH_DAY_NOTIME = 4,
    TIMESTAMP_FORMAT_MONTH_DAY_NOTIME = 5,
} timestamp_format;

// NOTE: This function expects a null-terminated string, NOT CRLF. This is based
// on the expectation that the recv() loop is overwriting the CRLFs as it
// receives messages from the server, which makes parsing the incoming socket
// data easier.
// If the provided string has a bad format, you'll get back NULL and a 
// NULL is returned if the provided string has a bad format, and a descriptive
// error is logged. Otherwise, you'll receive a pointer to a heap-allocated
// 'ircmsg' struct. Use msgutils_ircmsg_free() to deallocate it.
// The 'ircmsg' struct fields are not guaranteed to be validated.
ircmsg* msgutils_ircmsg_parse(char *raw_msg);

void msgutils_ircmsg_free(ircmsg *ircm);

bool msgutils_get_timestamp(
        char *const buf, size_t bufsize, bool utc, timestamp_format format);

