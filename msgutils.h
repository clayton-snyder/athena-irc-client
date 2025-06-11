#pragma once

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

ircmsg* msgutils_ircmsg_parse(char *raw_msg);

void msgutils_ircmsg_free(ircmsg *ircm);

void DEBUG_ircmsg_print(ircmsg *ircm);

bool msgutils_get_timestamp(
        char *const buf, rsize_t bufsize, bool utc, timestamp_format format);

bool msgutils_buildmsg_server(
        char *const buf, rsize_t bufsize, const char *const raw_msg);

bool msgutils_buildmsg_user(
        char *const buf, rsize_t bufsize, const char *const raw_msg);
