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

#pragma once

#include <stdbool.h>

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

void termutils_set_bold(bool on);
void termutils_set_faint(bool on);
void termutils_set_italic(bool on);
void termutils_set_underline(bool on);
void termutils_set_blinking(bool on);
void termutils_set_inverse(bool on);
void termutils_set_hidden(bool on);
void termutils_set_strikethrough(bool on);

// Sets the text (foreground) color using the 8-16 color set (some terminals may
// not support the _BRIGHT versions of colors).
void termutils_set_text_color(termutils_color color);

// Sets the text (foreground) color using the ANSI 256 color table code.
void termutils_set_text_color_256(int color_code);

// Same as calling `set_text_color(TERMUTILS_COLOR_DEFAULT);`
void termutils_reset_text_color();

// Sets the background color using the 8-16 color set (some terminals may not
// support the _BRIGHT versions of colors).
void termutils_set_bg_color(termutils_color color);

// Sets the background color using the ANSI 256 color table code.
void termutils_set_bg_color_256(int color_code);

// Same as calling `set_bg_color(TERMUTILS_COLOR_DEFAULT);`
void termutils_reset_bg_color();

// Resets all colors and formatting options to their defaults.
void termutils_reset_all();

// color_str can be a hex string or a color name. 
void termutils_set_cursor_color(const char* color_str);

// I have noticed this not working in some terminals (e.g., WezTerm). OSC 112
// should reset the cursor color to default, but I'm not sure how default gets
// set in the first place. It works on Windows Terminal.
void termutils_reset_cursor_color();
