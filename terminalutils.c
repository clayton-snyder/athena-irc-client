#include "terminalutils.h"

#include <assert.h>
#include <stdio.h>

void termutils_set_bold(bool on) {
    fputs(on ? "\033[1m" : "\033[22m", stdout);
}

void termutils_set_faint(bool on) {
    fputs(on ? "\033[2m" : "\033[22m", stdout);
}

void termutils_set_italic(bool on) {
    fputs(on ? "\033[3m" : "\033[23m", stdout);
}

void termutils_set_underline(bool on) {
    fputs(on ? "\033[4m" : "\033[24m", stdout);
}

void termutils_set_blinking(bool on) {
    fputs(on ? "\033[5m" : "\033[25m", stdout);
}

void termutils_set_inverse(bool on) {
    fputs(on ? "\033[7m" : "\033[27m", stdout);
}

void termutils_set_hidden(bool on) {
    fputs(on ? "\033[8m" : "\033[28m", stdout);
}

void termutils_set_strikethrough(bool on) {
    fputs(on ? "\033[9m" : "\033[29m", stdout);
}

void termutils_set_text_color(termutils_color color) {
    switch (color) {
    case TERMUTILS_COLOR_BLACK:
        fputs("\033[30m", stdout);
        break;
    case TERMUTILS_COLOR_RED:
        fputs("\033[31m", stdout);
        break;
    case TERMUTILS_COLOR_GREEN:
        fputs("\033[32m", stdout);
        break;
    case TERMUTILS_COLOR_YELLOW:
        fputs("\033[33m", stdout);
        break;
    case TERMUTILS_COLOR_BLUE:
        fputs("\033[34m", stdout);
        break;
    case TERMUTILS_COLOR_MAGENTA:
        fputs("\033[35m", stdout);
        break;
    case TERMUTILS_COLOR_CYAN:
        fputs("\033[36m\0", stdout);
        break;
    case TERMUTILS_COLOR_WHITE:
        fputs("\033[37m", stdout);
        break;
    case TERMUTILS_COLOR_BLACK_BRIGHT:
        fputs("\033[90m", stdout);
        break;
    case TERMUTILS_COLOR_RED_BRIGHT:
        fputs("\033[91m", stdout);
        break;
    case TERMUTILS_COLOR_GREEN_BRIGHT:
        fputs("\033[92m", stdout);
        break;
    case TERMUTILS_COLOR_YELLOW_BRIGHT:
        fputs("\033[93m", stdout);
        break;
    case TERMUTILS_COLOR_BLUE_BRIGHT:
        fputs("\033[94m", stdout);
        break;
    case TERMUTILS_COLOR_MAGENTA_BRIGHT:
        fputs("\033[95m", stdout);
        break;
    case TERMUTILS_COLOR_CYAN_BRIGHT:
        fputs("\033[96m", stdout);
        break;
    case TERMUTILS_COLOR_WHITE_BRIGHT:
        fputs("\033[97m", stdout);
        break;
    case TERMUTILS_COLOR_DEFAULT:
        fputs("\033[39m", stdout);
        break;
    default:
#ifdef TERMUTILS_DEBUG_ASSERT
        assert(!"[set_text_color()] Invalid TERMUTILS_COLOR");
#endif
        return;
    }
}

void termutils_set_text_color_256(int color_code) {
#ifdef TERMUTILS_DEBUG_ASSERT
    assert(color_code >= 0 && color_code <= 255);
#endif
    if (color_code < 0 || color_code > 255) return;
    printf("\033[38;5;%dm", color_code);
}

void termutils_reset_text_color() {
    fputs("\033[39m", stdout);
}

void termutils_set_bg_color(termutils_color color) {
    switch (color) {
    case TERMUTILS_COLOR_BLACK:
        fputs("\033[40m", stdout);
        break;
    case TERMUTILS_COLOR_RED:
        fputs("\033[41m", stdout);
        break;
    case TERMUTILS_COLOR_GREEN:
        fputs("\033[42m", stdout);
        break;
    case TERMUTILS_COLOR_YELLOW:
        fputs("\033[43m", stdout);
        break;
    case TERMUTILS_COLOR_BLUE:
        fputs("\033[44m", stdout);
        break;
    case TERMUTILS_COLOR_MAGENTA:
        fputs("\033[45m", stdout);
        break;
    case TERMUTILS_COLOR_CYAN:
        fputs("\033[46m", stdout);
        break;
    case TERMUTILS_COLOR_WHITE:
        fputs("\033[47m", stdout);
        break;
    case TERMUTILS_COLOR_BLACK_BRIGHT:
        fputs("\033[100m", stdout);
        break;
    case TERMUTILS_COLOR_RED_BRIGHT:
        fputs("\033[101m", stdout);
        break;
    case TERMUTILS_COLOR_GREEN_BRIGHT:
        fputs("\033[102m", stdout);
        break;
    case TERMUTILS_COLOR_YELLOW_BRIGHT:
        fputs("\033[103m", stdout);
        break;
    case TERMUTILS_COLOR_BLUE_BRIGHT:
        fputs("\033[104m", stdout);
        break;
    case TERMUTILS_COLOR_MAGENTA_BRIGHT:
        fputs("\033[105m", stdout);
        break;
    case TERMUTILS_COLOR_CYAN_BRIGHT:
        fputs("\033[106m", stdout);
        break;
    case TERMUTILS_COLOR_WHITE_BRIGHT:
        fputs("\033[107m", stdout);
        break;
    case TERMUTILS_COLOR_DEFAULT:
        fputs("\033[49m", stdout);
        break;
    default:
#ifdef TERMUTILS_DEBUG_ASSERT
        assert(!"[set_bg_color()] Invalid TERMUTILS_COLOR");
#endif
        return;
    }
}

void termutils_set_bg_color_256(int color_code) {
#ifdef TERMUTILS_DEBUG_ASSERT
    assert(color_code >= 0 && color_code <= 255);
#endif
    if (color_code < 0 || color_code > 255) return;
    printf("\033[48;5;%dm", color_code);
}

void termutils_reset_bg_color() {
    fputs("\033[49m", stdout);
}

void termutils_reset_all() {
    fputs("\033[0m", stdout);
}

void termutils_set_cursor_color(const char* color_str) {
    printf("\x1b]12;%s\x07", color_str);
}

void termutils_reset_cursor_color() {
    printf("\x1b]112\x07");
}
