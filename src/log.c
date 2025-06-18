#include "log.h"

#include "terminalutils.h"

#include <assert.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>

static loglevel logger_level = LOGLEVEL_WARNING;
static FILE *s_out = NULL;
static bool s_initialized = false;

// Sets the text color/format and prints the prefix for the provided severity.
// You still need to call termutils_reset_all() when you're done printing. Call
// once per log message, even for the same severity.
static void prepare_console(loglevel severity);

void log_init(FILE *const log_to) {
    set_log_file(log_to == NULL ? stdout : log_to);
    s_initialized = true;
}

void set_logger_level(loglevel severity) {
    logger_level = severity;
}

void set_log_file(FILE *const log_to) {
    if (log_to == NULL) s_out = stdout;
    else s_out = log_to;
}

void log(loglevel severity, const char *const msg) {
    assert(s_initialized);
    if (logger_level < severity) return;

    prepare_console(severity);
    fprintf_s(s_out, "%s\n", msg);

    termutils_reset_all(s_out);
}

void log_fmt(loglevel severity, const char *const fmt_str, ...) {
    assert(s_initialized);
    if (logger_level < severity) return;

    va_list fmt_args;
    va_start(fmt_args, fmt_str);

    prepare_console(severity);

    vfprintf_s(s_out, fmt_str, fmt_args);
    va_end(fmt_args);
    fputs("\n", s_out);
    termutils_reset_all(s_out);// TODO: might have to be before the newline...
}

static void prepare_console(loglevel severity) {
    switch (severity) {
    case LOGLEVEL_ERROR:
        termutils_set_text_color(TERMUTILS_COLOR_RED, s_out);
        termutils_set_bold(true, s_out);
        fputs("<ERROR>   ", s_out);
        termutils_set_bold(false, s_out);
        break;
    case LOGLEVEL_WARNING:
        termutils_set_text_color(TERMUTILS_COLOR_YELLOW, s_out);
        termutils_set_bold(true, s_out);
        fputs("<WARNING> ", s_out);
        termutils_set_bold(false, s_out);
        break;
    case LOGLEVEL_INFO:
        fputs("<INFO>    ", s_out);
        break;
    case LOGLEVEL_DEV:
        termutils_set_text_color_256(250, s_out); // Light gray
        fputs("<DEV>     ", s_out);
        break;
    case LOGLEVEL_SPAM:
        termutils_set_text_color_256(239, s_out); // Dark gray
        fputs("<spam>    ", s_out);
        break;
    default:
        termutils_set_text_color(TERMUTILS_COLOR_MAGENTA, s_out);
        termutils_set_bold(true, s_out);
        fputs("<UNKNOWN LOGLEVEL> ", s_out);
        termutils_set_bold(false, s_out);
    }
}
