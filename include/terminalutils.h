// This library uses ANSI escape codes, a widely used cross-platform standard
// for in-band text formatting used by virtually all modern terminal emulators.
//
//      WINDOWS
// Windows Terminal supports these, but the classic Windows Console Host
// (conhost.exe - which may be the default depending on user settings) does not,
// so they will be printed as plaintext. If you need your program to run on the
// old-school Windows Console, don't use this library.
//
// Microsoft is gradually moving away from the classic Windows Console host in
// favor of Windows Terminal, which is now the built-in default on Windows 11.
// See: https://learn.microsoft.com/en-us/windows/console/ecosystem-roadmap
//
//      ASSERTS
// To enable debug asserts, define the TERMUTILS_DEBUG_ASSERT macro. This will
// assert validity of the color code and enum values passed in. Otherwise, if
// invalid values are passed, the function will quietly abort.
//
//      PRINT vs. WRITE TO BUFFER
// Functions with the "_buf" suffix take a char* and a max length and write the
// ANSI escape sequence into the char*, returning false if the sequence could 
// not fit in the provided space. Functions without the suffix simply print the
// format string to stdout. The set of functions is identical for both. Clearly,
// using the "_buf" functions will generally be more efficient, assuming you
// plan to write multiple escape sequences into the buffer between each print.
// Use the buffer pattern when doing heavy formatting.

#pragma once

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

typedef enum {
    TERMUTILS_COLOR_BLACK,
    TERMUTILS_COLOR_RED,
    TERMUTILS_COLOR_GREEN,
    TERMUTILS_COLOR_YELLOW,
    TERMUTILS_COLOR_BLUE,
    TERMUTILS_COLOR_MAGENTA,
    TERMUTILS_COLOR_CYAN,
    TERMUTILS_COLOR_WHITE,
    TERMUTILS_COLOR_BLACK_BRIGHT,
    TERMUTILS_COLOR_RED_BRIGHT,
    TERMUTILS_COLOR_GREEN_BRIGHT,
    TERMUTILS_COLOR_YELLOW_BRIGHT,
    TERMUTILS_COLOR_BLUE_BRIGHT,
    TERMUTILS_COLOR_MAGENTA_BRIGHT,
    TERMUTILS_COLOR_CYAN_BRIGHT,
    TERMUTILS_COLOR_WHITE_BRIGHT,
    TERMUTILS_COLOR_DEFAULT
} termutils_color;

size_t termutils_set_bold_buf(char *const buf, size_t maxlen, bool on);
size_t termutils_set_faint_buf(char *const buf, size_t maxlen, bool on);
size_t termutils_set_italic_buf(char *const buf, size_t maxlen, bool on);
size_t termutils_set_underline_buf(char *const buf, size_t maxlen, bool on);
size_t termutils_set_blinking_buf(char *const buf, size_t maxlen, bool on);
size_t termutils_set_inverse_buf(char *const buf, size_t maxlen, bool on);
size_t termutils_set_hidden_buf(char *const buf, size_t maxlen, bool on);
size_t termutils_set_strikethrough_buf(
        char *const buf, size_t maxlen, bool on);

// Sets the text (foreground) color using the 8-16 color set (some terminals may
// not support the _BRIGHT versions of colors).
size_t termutils_set_text_color_buf(
        char *const buf, size_t maxlen, termutils_color color);

// Sets the text (foreground) color using the ANSI 256 color table code.
size_t termutils_set_text_color_256_buf(
        char *const buf, size_t maxlen, int color_code);

// Same as calling `set_text_color(TERMUTILS_COLOR_DEFAULT);`
size_t termutils_reset_text_color_buf(char *const buf, size_t maxlen);

// Sets the background color using the 8-16 color set (some terminals may not
// support the _BRIGHT versions of colors).
size_t termutils_set_bg_color_buf(
        char *const buf, size_t maxlen, termutils_color color);

// Sets the background color using the ANSI 256 color table code.
size_t termutils_set_bg_color_256_buf(
        char *const buf, size_t maxlen, int color_code);

// Same as calling `set_bg_color(TERMUTILS_COLOR_DEFAULT);`
size_t termutils_reset_bg_color_buf(char *const buf, size_t maxlen);

// Resets all colors and formatting options to their defaults.
size_t termutils_reset_all_buf(char *const buf, size_t maxlen);

// color_str can be a hex string or a color name. 
size_t termutils_set_cursor_color_buf(
        char *const buf, size_t maxlen, const char *const color_str);

// I have noticed this not working in some terminals (e.g., WezTerm). OSC 112
// should reset the cursor color to default, but I'm not sure how default gets
// set in the first place. It works on Windows Terminal.
size_t termutils_reset_cursor_color_buf(char *const buf, size_t maxlen);

void termutils_set_bold(bool on, FILE *const out);
void termutils_set_faint(bool on, FILE *const out);
void termutils_set_italic(bool on, FILE *const out);
void termutils_set_underline(bool on, FILE *const out);
void termutils_set_blinking(bool on, FILE *const out);
void termutils_set_inverse(bool on, FILE *const out);
void termutils_set_hidden(bool on, FILE *const out);
void termutils_set_strikethrough(bool on, FILE *const out);

// Sets the text (foreground) color using the 8-16 color set (some terminals may
// not support the _BRIGHT versions of colors).
void termutils_set_text_color(termutils_color color, FILE *const out);

// Sets the text (foreground) color using the ANSI 256 color table code.
void termutils_set_text_color_256(int color_code, FILE *const out);

// Same as calling `set_text_color(TERMUTILS_COLOR_DEFAULT);`
void termutils_reset_text_color(FILE *const out);

// Sets the background color using the 8-16 color set (some terminals may not
// support the _BRIGHT versions of colors).
void termutils_set_bg_color(termutils_color color, FILE *const out);

// Sets the background color using the ANSI 256 color table code.
void termutils_set_bg_color_256(int color_code, FILE *const out);

// Same as calling `set_bg_color(TERMUTILS_COLOR_DEFAULT);`
void termutils_reset_bg_color(FILE *const out);

// Resets all colors and formatting options to their defaults.
void termutils_reset_all(FILE *const out);

// color_str can be a hex string or a color name. 
void termutils_set_cursor_color(const char* color_str, FILE *const out);

// I have noticed this not working in some terminals (e.g., WezTerm). OSC 112
// should reset the cursor color to default, but I'm not sure how default gets
// set in the first place. It works on Windows Terminal.
void termutils_reset_cursor_color(FILE *const out);
