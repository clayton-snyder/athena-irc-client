#include "terminalutils.h"

#include <assert.h>
#include <stdio.h>

// Character to begin an ANSI escape sequence
#define ESC "\033"

// ESC"[38;5;255m". Last value is 0-255, and the rest is static. 
#define ESC_SEQ_COLOR_256_MAX_LEN 11

// color_str refers to a hex string or a color name. The longest color names are
// "yellow" and "purple", shorter than "#FFFFFF".
#define MAX_COLOR_STR_LEN 7

// ESC"]12;#FFFFFF"ESC. 
#define ESC_SEQ_CURSOR_COLOR_MAX_LEN (6 + MAX_COLOR_STR_LEN)

// Writes 'str' to 'buf', including null term, if strlen(str) is less than
// 'maxlen'. Returns number of chars written, not including the null term, or 0
// if 'str' could not be written.
static size_t write_if_fits(
        char *const buf, size_t maxlen, const char *const str);

size_t termutils_set_bold_buf(char *const buf, size_t maxlen, bool on) {
    return write_if_fits(buf, maxlen, on ? ESC"[1m" : ESC"[22m");
}

size_t termutils_set_faint_buf(char *const buf, size_t maxlen, bool on) {
    return write_if_fits(buf, maxlen, on ? ESC"[2m" : ESC"[22m");
}

size_t termutils_set_italic_buf(char *const buf, size_t maxlen, bool on) {
    return write_if_fits(buf, maxlen, on ? "\033[3m" : "\033[23m");
}

size_t termutils_set_underline_buf(char *const buf, size_t maxlen, bool on) {
    return write_if_fits(buf, maxlen, on ? "\033[4m" : "\033[24m");
}

size_t termutils_set_blinking_buf(char *const buf, size_t maxlen, bool on) {
    return write_if_fits(buf, maxlen, on ? "\033[5m" : "\033[25m");
}

size_t termutils_set_inverse_buf(char *const buf, size_t maxlen, bool on) {
    return write_if_fits(buf, maxlen, on ? "\033[7m" : "\033[27m");
}

size_t termutils_set_hidden_buf(char *const buf, size_t maxlen, bool on) {
    return write_if_fits(buf, maxlen, on ? "\033[8m" : "\033[28m");
}

size_t termutils_set_striketh_buf(char *const buf, size_t maxlen, bool on) {
    return write_if_fits(buf, maxlen, on ? "\033[9m" : "\033[29m");
}

size_t termutils_set_text_color_buf(
        char *const buf, size_t maxlen, termutils_color color)
{
    switch (color) {
    case TERMUTILS_COLOR_BLACK:
        return write_if_fits(buf, maxlen, "\033[30m");
    case TERMUTILS_COLOR_RED:
        return write_if_fits(buf, maxlen, "\033[31m");
    case TERMUTILS_COLOR_GREEN:
        return write_if_fits(buf, maxlen, "\033[32m");
    case TERMUTILS_COLOR_YELLOW:
        return write_if_fits(buf, maxlen, "\033[33m");
    case TERMUTILS_COLOR_BLUE:
        return write_if_fits(buf, maxlen, "\033[34m");
    case TERMUTILS_COLOR_MAGENTA:
        return write_if_fits(buf, maxlen, "\033[35m");
    case TERMUTILS_COLOR_CYAN:
        return write_if_fits(buf, maxlen, "\033[36m\0");
    case TERMUTILS_COLOR_WHITE:
        return write_if_fits(buf, maxlen, "\033[37m");
    case TERMUTILS_COLOR_BLACK_BRIGHT:
        return write_if_fits(buf, maxlen, "\033[90m");
    case TERMUTILS_COLOR_RED_BRIGHT:
        return write_if_fits(buf, maxlen, "\033[91m");
    case TERMUTILS_COLOR_GREEN_BRIGHT:
        return write_if_fits(buf, maxlen, "\033[92m");
    case TERMUTILS_COLOR_YELLOW_BRIGHT:
        return write_if_fits(buf, maxlen, "\033[93m");
    case TERMUTILS_COLOR_BLUE_BRIGHT:
        return write_if_fits(buf, maxlen, "\033[94m");
    case TERMUTILS_COLOR_MAGENTA_BRIGHT:
        return write_if_fits(buf, maxlen, "\033[95m");
    case TERMUTILS_COLOR_CYAN_BRIGHT:
        return write_if_fits(buf, maxlen, "\033[96m");
    case TERMUTILS_COLOR_WHITE_BRIGHT:
        return write_if_fits(buf, maxlen, "\033[97m");
    case TERMUTILS_COLOR_DEFAULT:
        return write_if_fits(buf, maxlen, "\033[39m");
    default:
#ifdef TERMUTILS_DEBUG_ASSERT
        assert(!"[set_text_color()] Invalid TERMUTILS_COLOR");
#endif
        return false;
    }
}

size_t termutils_set_text_color_256_buf(
        char *const buf, size_t maxlen, int color_code)
{
#ifdef TERMUTILS_DEBUG_ASSERT
    assert(color_code >= 0 && color_code <= 255);
    // don't call this if you don't have room
    assert(maxlen > ESC_SEQ_COLOR_256_MAX_LEN);
#endif
    if (maxlen <= ESC_SEQ_COLOR_256_MAX_LEN) return 0;

    char seq[ESC_SEQ_COLOR_256_MAX_LEN + 1];
    int len = sprintf_s(seq, sizeof(seq), ESC"[38;5;%dm", color_code);

#ifdef TERMUTILS_DEBUG_ASSERT
    assert(len <= ESC_SEQ_COLOR_256_MAX_LEN);
#endif
    if (len > (int) maxlen) return 0;
    return write_if_fits(buf, maxlen, seq);
}

size_t termutils_reset_text_color_buf(char *const buf, size_t maxlen) {
    return write_if_fits(buf, maxlen, "\033[39m");
}

size_t termutils_set_bg_color_buf(
        char *const buf, size_t maxlen, termutils_color color)
{
    switch (color) {
    case TERMUTILS_COLOR_BLACK:
        return write_if_fits(buf, maxlen, "\033[40m");
    case TERMUTILS_COLOR_RED:
        return write_if_fits(buf, maxlen, "\033[41m");
    case TERMUTILS_COLOR_GREEN:
        return write_if_fits(buf, maxlen, "\033[42m");
    case TERMUTILS_COLOR_YELLOW:
        return write_if_fits(buf, maxlen, "\033[43m");
    case TERMUTILS_COLOR_BLUE:
        return write_if_fits(buf, maxlen, "\033[44m");
    case TERMUTILS_COLOR_MAGENTA:
        return write_if_fits(buf, maxlen, "\033[45m");
    case TERMUTILS_COLOR_CYAN:
        return write_if_fits(buf, maxlen, "\033[46m");
    case TERMUTILS_COLOR_WHITE:
        return write_if_fits(buf, maxlen, "\033[47m");
    case TERMUTILS_COLOR_BLACK_BRIGHT:
        return write_if_fits(buf, maxlen, "\033[100m");
    case TERMUTILS_COLOR_RED_BRIGHT:
        return write_if_fits(buf, maxlen, "\033[101m");
    case TERMUTILS_COLOR_GREEN_BRIGHT:
        return write_if_fits(buf, maxlen, "\033[102m");
    case TERMUTILS_COLOR_YELLOW_BRIGHT:
        return write_if_fits(buf, maxlen, "\033[103m");
    case TERMUTILS_COLOR_BLUE_BRIGHT:
        return write_if_fits(buf, maxlen, "\033[104m");
    case TERMUTILS_COLOR_MAGENTA_BRIGHT:
        return write_if_fits(buf, maxlen, "\033[105m");
    case TERMUTILS_COLOR_CYAN_BRIGHT:
        return write_if_fits(buf, maxlen, "\033[106m");
    case TERMUTILS_COLOR_WHITE_BRIGHT:
        return write_if_fits(buf, maxlen, "\033[107m");
    case TERMUTILS_COLOR_DEFAULT:
        return write_if_fits(buf, maxlen, "\033[49m");
    default:
#ifdef TERMUTILS_DEBUG_ASSERT
        assert(!"[set_bg_color()] Invalid TERMUTILS_COLOR");
#endif
        return false;
    }
}

size_t termutils_set_bg_color_256_buf(
        char *const buf, size_t maxlen, int color_code)
{
#ifdef TERMUTILS_DEBUG_ASSERT
    assert(color_code >= 0 && color_code <= 255);
    // don't call this if you don't have room
    assert(maxlen > ESC_SEQ_COLOR_256_MAX_LEN);
#endif
    if (maxlen <= ESC_SEQ_COLOR_256_MAX_LEN) return 0;

    char seq[ESC_SEQ_COLOR_256_MAX_LEN + 1];
    int len = sprintf_s(seq, sizeof(seq), ESC"[48;5;%dm", color_code);

#ifdef TERMUTILS_DEBUG_ASSERT
    assert(len <= ESC_SEQ_COLOR_256_MAX_LEN);
#endif
    if (len > (int) maxlen) return 0;
    return write_if_fits(buf, maxlen, seq);
}

size_t termutils_reset_bg_color_buf(char *const buf, size_t maxlen) {
    return write_if_fits(buf, maxlen, "\033[49m");
}

size_t termutils_reset_all_buf(char *const buf, size_t maxlen) {
    return write_if_fits(buf, maxlen, "\033[0m");
}

size_t termutils_set_cursor_color_buf(
        char *const buf, size_t maxlen, const char *const color_str)
{
#ifdef TERMUTILS_DEBUG_ASSERT
    assert(strlen(color_str) < MAX_COLOR_STR_LEN);
#endif
    if (strlen(color_str) > MAX_COLOR_STR_LEN) return false;

#ifdef TERMUTILS_DEBUG_ASSERT
    assert(maxlen >= ESC_SEQ_CURSOR_COLOR_MAX_LEN);
#endif
    if (maxlen < ESC_SEQ_CURSOR_COLOR_MAX_LEN) return false;

    char seq[ESC_SEQ_CURSOR_COLOR_MAX_LEN];
    sprintf_s(seq, sizeof(seq), "\x1b]12;%s\x07", color_str);

    return write_if_fits(buf, maxlen, seq);
}

size_t termutils_reset_cursor_color_buf(char *const buf, size_t maxlen) {
    return write_if_fits(buf, maxlen, "\x1b]112\x07");
}

void termutils_set_bold(bool on, FILE *const out) {
    fputs(on ? "\033[1m" : "\033[22m", out);
}

void termutils_set_faint(bool on, FILE *const out) {
    fputs(on ? "\033[2m" : "\033[22m", out);
}

void termutils_set_italic(bool on, FILE *const out) {
    fputs(on ? "\033[3m" : "\033[23m", out);
}

void termutils_set_underline(bool on, FILE *const out) {
    fputs(on ? "\033[4m" : "\033[24m", out);
}

void termutils_set_blinking(bool on, FILE *const out) {
    fputs(on ? "\033[5m" : "\033[25m", out);
}

void termutils_set_inverse(bool on, FILE *const out) {
    fputs(on ? "\033[7m" : "\033[27m", out);
}

void termutils_set_hidden(bool on, FILE *const out) {
    fputs(on ? "\033[8m" : "\033[28m", out);
}

void termutils_set_striketh(bool on, FILE *const out) {
    fputs(on ? "\033[9m" : "\033[29m", out);
}

void termutils_set_text_color(termutils_color color, FILE *const out) {
    switch (color) {
    case TERMUTILS_COLOR_BLACK:
        fputs("\033[30m", out);
        break;
    case TERMUTILS_COLOR_RED:
        fputs("\033[31m", out);
        break;
    case TERMUTILS_COLOR_GREEN:
        fputs("\033[32m", out);
        break;
    case TERMUTILS_COLOR_YELLOW:
        fputs("\033[33m", out);
        break;
    case TERMUTILS_COLOR_BLUE:
        fputs("\033[34m", out);
        break;
    case TERMUTILS_COLOR_MAGENTA:
        fputs("\033[35m", out);
        break;
    case TERMUTILS_COLOR_CYAN:
        fputs("\033[36m\0", out);
        break;
    case TERMUTILS_COLOR_WHITE:
        fputs("\033[37m", out);
        break;
    case TERMUTILS_COLOR_BLACK_BRIGHT:
        fputs("\033[90m", out);
        break;
    case TERMUTILS_COLOR_RED_BRIGHT:
        fputs("\033[91m", out);
        break;
    case TERMUTILS_COLOR_GREEN_BRIGHT:
        fputs("\033[92m", out);
        break;
    case TERMUTILS_COLOR_YELLOW_BRIGHT:
        fputs("\033[93m", out);
        break;
    case TERMUTILS_COLOR_BLUE_BRIGHT:
        fputs("\033[94m", out);
        break;
    case TERMUTILS_COLOR_MAGENTA_BRIGHT:
        fputs("\033[95m", out);
        break;
    case TERMUTILS_COLOR_CYAN_BRIGHT:
        fputs("\033[96m", out);
        break;
    case TERMUTILS_COLOR_WHITE_BRIGHT:
        fputs("\033[97m", out);
        break;
    case TERMUTILS_COLOR_DEFAULT:
        fputs("\033[39m", out);
        break;
    default:
#ifdef TERMUTILS_DEBUG_ASSERT
        assert(!"[set_text_color()] Invalid TERMUTILS_COLOR");
#endif
        return;
    }
}

void termutils_set_text_color_256(int color_code, FILE *const out) {
#ifdef TERMUTILS_DEBUG_ASSERT
    assert(color_code >= 0 && color_code <= 255);
#endif
    fprintf_s(out, "\033[38;5;%dm", color_code);
}

void termutils_reset_text_color(FILE *const out) {
    fputs("\033[39m", out);
}

void termutils_set_bg_color(termutils_color color, FILE *const out) {
    switch (color) {
    case TERMUTILS_COLOR_BLACK:
        fputs("\033[40m", out);
        break;
    case TERMUTILS_COLOR_RED:
        fputs("\033[41m", out);
        break;
    case TERMUTILS_COLOR_GREEN:
        fputs("\033[42m", out);
        break;
    case TERMUTILS_COLOR_YELLOW:
        fputs("\033[43m", out);
        break;
    case TERMUTILS_COLOR_BLUE:
        fputs("\033[44m", out);
        break;
    case TERMUTILS_COLOR_MAGENTA:
        fputs("\033[45m", out);
        break;
    case TERMUTILS_COLOR_CYAN:
        fputs("\033[46m", out);
        break;
    case TERMUTILS_COLOR_WHITE:
        fputs("\033[47m", out);
        break;
    case TERMUTILS_COLOR_BLACK_BRIGHT:
        fputs("\033[100m", out);
        break;
    case TERMUTILS_COLOR_RED_BRIGHT:
        fputs("\033[101m", out);
        break;
    case TERMUTILS_COLOR_GREEN_BRIGHT:
        fputs("\033[102m", out);
        break;
    case TERMUTILS_COLOR_YELLOW_BRIGHT:
        fputs("\033[103m", out);
        break;
    case TERMUTILS_COLOR_BLUE_BRIGHT:
        fputs("\033[104m", out);
        break;
    case TERMUTILS_COLOR_MAGENTA_BRIGHT:
        fputs("\033[105m", out);
        break;
    case TERMUTILS_COLOR_CYAN_BRIGHT:
        fputs("\033[106m", out);
        break;
    case TERMUTILS_COLOR_WHITE_BRIGHT:
        fputs("\033[107m", out);
        break;
    case TERMUTILS_COLOR_DEFAULT:
        fputs("\033[49m", out);
        break;
    default:
#ifdef TERMUTILS_DEBUG_ASSERT
        assert(!"[set_bg_color()] Invalid TERMUTILS_COLOR");
#endif
        return;
    }
}

void termutils_set_bg_color_256(int color_code, FILE *const out) {
#ifdef TERMUTILS_DEBUG_ASSERT
    assert(color_code >= 0 && color_code <= 255);
#endif
    fprintf_s(out, "\033[48;5;%dm", color_code);
}

void termutils_reset_bg_color(FILE *const out) {
    fputs("\033[49m", out);
}

void termutils_reset_all(FILE *const out) {
    fputs("\033[0m", out);
}

void termutils_set_cursor_color(const char* color_str, FILE *const out) {
    fprintf_s(out, "\x1b]12;%s\x07", color_str);
}

void termutils_reset_cursor_color(FILE *const out) {
    fprintf_s(out, "\x1b]112\x07");
}

static size_t write_if_fits(
        char *const buf, size_t maxlen, const char *const str)
{
    assert(buf != NULL);
    assert(str != NULL);

    size_t len = strlen(str);
    if (len >= maxlen) return 0;

    if (strcpy_s(buf, maxlen, str) != 0) return 0;

    return len;
}
