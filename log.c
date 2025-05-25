#include "log.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>

static loglevel logger_level = LOGLEVEL_WARNING;

typedef enum {
    TEXTCOLOR_BLACK = 0,
    TEXTCOLOR_RED = 1,
    TEXTCOLOR_YELLOW = 2,
    TEXTCOLOR_GRAY = 3,
    TEXTCOLOR_LIGHTGRAY = 4,
    TEXTCOLOR_MAGENTA = 5
} textcolor;

// ANSI color codes
static const char* color_codes[] = {
    [TEXTCOLOR_BLACK] = "30",
    [TEXTCOLOR_RED] = "31",
    [TEXTCOLOR_YELLOW] = "33",
    [TEXTCOLOR_GRAY] = "38;5;242",
    [TEXTCOLOR_LIGHTGRAY] = "38;5;249",
    [TEXTCOLOR_MAGENTA] = "35"
};

// Every set_color() call should be matched by a reset_color() when the log 
// message has been printed.
static void set_color(textcolor color, bool bold);
static void reset_color();

// Sets the text color and prints prefix for the provided severity. You still
// need to call reset_color() when you're done printing the log message.
// Call once per log message, even for the same severity.
static void prepare_console(loglevel severity);

void set_logger_level(loglevel severity) {
    logger_level = severity;
}

void log(loglevel severity, char *msg) {
    if (logger_level < severity) return;
    prepare_console(severity);
    printf(msg);
    reset_color();
    printf("\n");
}

void log_fmt(loglevel severity, char *fmt_str, ...) {
    if (logger_level < severity) return;

    va_list fmt_args;
    va_start(fmt_args, fmt_str);

    prepare_console(severity);

    vprintf(fmt_str, fmt_args);
    va_end(fmt_args);
    reset_color();
    printf("\n");
}

static void set_color(textcolor color, bool bold) {
    printf("\033[%d;%sm", bold, color_codes[color]);
}

static void reset_color() {
    printf("\033[0m");
}

static void prepare_console(loglevel severity) {
    switch (severity) {
    case LOGLEVEL_ERROR:
        set_color(TEXTCOLOR_RED, true);
        printf("<ERROR>   ");
        set_color(TEXTCOLOR_RED, false);
        break;
    case LOGLEVEL_WARNING:
        set_color(TEXTCOLOR_YELLOW, true);
        printf("<WARNING> ");
        set_color(TEXTCOLOR_YELLOW, false);
        break;
    case LOGLEVEL_INFO:
        printf("<INFO>    ");
        break;
    case LOGLEVEL_DEV:
        set_color(TEXTCOLOR_LIGHTGRAY, false);
        printf("<DEV>     ");
        break;
    case LOGLEVEL_SPAM:
        set_color(TEXTCOLOR_GRAY, false);
        printf("<spam>    ");
        break;
    default:
        set_color(TEXTCOLOR_MAGENTA, true);
        printf("<UNKNOWN LOGLEVEL> ");
        set_color(TEXTCOLOR_MAGENTA, false);
    }
}
