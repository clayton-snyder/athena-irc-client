#pragma once

typedef enum {
    LOGLEVEL_ERROR = 1,
    LOGLEVEL_WARNING = 2,
    LOGLEVEL_INFO = 3,
    LOGLEVEL_DEV = 4,
    LOGLEVEL_SPAM = 5
} loglevel;

void set_logger_level(loglevel level);

void log(loglevel severity, char *msg);

void log_fmt(loglevel severity, char *fmt_str, ...);

