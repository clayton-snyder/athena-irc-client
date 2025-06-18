#pragma once

#include <stdio.h>

typedef enum {
    LOGLEVEL_ERROR = 1,
    LOGLEVEL_WARNING = 2,
    LOGLEVEL_INFO = 3,
    LOGLEVEL_DEV = 4,
    LOGLEVEL_SPAM = 5
} loglevel;

// Must be called before using any log functions. If log_to is NULL, logs will
// be written to stdout.
void log_init(FILE *const log_to);

void set_logger_level(loglevel level);

void set_log_file(FILE *const log_to);

void log(loglevel severity, const char *const msg);

void log_fmt(loglevel severity, const char *const fmt_str, ...);

