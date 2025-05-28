#include "log.h"

#include "terminalutils.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>

static loglevel logger_level = LOGLEVEL_WARNING;

// Sets the text color and prints prefix for the provided severity. You still
// need to call termutils_reset_all() when you're done printing the log message.
// Call once per log message, even for the same severity.
static void prepare_console(loglevel severity);

void set_logger_level(loglevel severity) {
    logger_level = severity;
}

void log(loglevel severity, char *msg) {
    if (logger_level < severity) return;
    prepare_console(severity);
    puts(msg);
    termutils_reset_all();
}

void log_fmt(loglevel severity, char *fmt_str, ...) {
    if (logger_level < severity) return;

    va_list fmt_args;
    va_start(fmt_args, fmt_str);

    prepare_console(severity);

    vprintf(fmt_str, fmt_args);
    va_end(fmt_args);
    printf("\n");
    termutils_reset_all();// TODO: might have to be before the newline...
}

static void prepare_console(loglevel severity) {
    switch (severity) {
    case LOGLEVEL_ERROR:
        termutils_set_text_color(TERMUTILS_COLOR_RED);
        termutils_set_bold(true);
        printf("<ERROR>   ");
        termutils_set_bold(false);
        break;
    case LOGLEVEL_WARNING:
        termutils_set_text_color(TERMUTILS_COLOR_YELLOW);
        termutils_set_bold(true);
        printf("<WARNING> ");
        termutils_set_bold(false);
        break;
    case LOGLEVEL_INFO:
        printf("<INFO>    ");
        break;
    case LOGLEVEL_DEV:
        termutils_set_text_color_256(250); // Light gray
        printf("<DEV>     ");
        break;
    case LOGLEVEL_SPAM:
        termutils_set_text_color_256(239); // Dark gray
        printf("<spam>    ");
        break;
    default:
        termutils_set_text_color(TERMUTILS_COLOR_MAGENTA);
        termutils_set_bold(true);
        printf("<UNKNOWN LOGLEVEL> ");
        termutils_set_bold(false);
    }
}
