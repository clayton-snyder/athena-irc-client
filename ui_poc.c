#include <stdbool.h>
#include <stdio.h>
#include <windows.h>

DWORD WINAPI ui_main(LPVOID data);

int quit = false;

int main() {
    printf("\033[?1049h");

    HANDLE h_stdin = GetStdHandle(STD_INPUT_HANDLE);
    DWORD mode;
    GetConsoleMode(h_stdin, &mode);
    mode |= (ENABLE_EXTENDED_FLAGS  
        | ENABLE_WINDOW_INPUT  
        | ENABLE_MOUSE_INPUT 
        | ENABLE_PROCESSED_INPUT);
    if (!SetConsoleMode(h_stdin, mode)) {
        printf("SetConsoleMode failed\n");
        return -1;
    }

    DWORD thread_id = -1;
    HANDLE ui_thread = CreateThread(NULL, 0, ui_main, NULL, 0, &thread_id);
    while (!quit) {
    }
    printf("\033[?1049l");
    return 0;
}

DWORD WINAPI ui_main(LPVOID data) {
    char screenbuf[500];
    size_t i_screenbuf = 0;
    char uibuf[50];
    size_t i_uibuf = 0;
    bool user_quit = false;

    int scroll = 0;
    HANDLE h_stdin = GetStdHandle(STD_INPUT_HANDLE);
    HANDLE h_stdout = GetStdHandle(STD_OUTPUT_HANDLE);

    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(h_stdout, &csbi);

    int columns = csbi.srWindow.Right - csbi.srWindow.Left + 1;
    int rows = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;

    INPUT_RECORD irbuf[128];
    DWORD events = 0;
    while (!user_quit) {
        ReadConsoleInput(h_stdin, irbuf, 128, &events);
        for (DWORD i = 0; i < events; i++) {

            if (irbuf[i].EventType == MOUSE_EVENT) {
                MOUSE_EVENT_RECORD m = irbuf[i].Event.MouseEvent;
                // Process the mouse event
                if (!(m.dwEventFlags & MOUSE_WHEELED)) continue;
                bool is_hw_negative = HIWORD(m.dwButtonState) & 1<<15;
                //screenbuf[i_screenbuf++] = is_hw_negative ? 'd' : 'u';
                is_hw_negative ? scroll-- : scroll++;
                if (is_hw_negative) { if (scroll > 0) scroll--; }
                else scroll++;

                continue;
            }
            if (irbuf[i].EventType != KEY_EVENT) continue;
            
            // KEY EVENTS
            KEY_EVENT_RECORD k = irbuf[i].Event.KeyEvent;
            if (!k.bKeyDown) continue;

            // Here we know it's a keydown event.

            if (k.wVirtualKeyCode == VK_BACK && i_uibuf > 0)
                uibuf[--i_uibuf] = '\0';
            if (k.wVirtualKeyCode == VK_ESCAPE)
                user_quit = true; // don't break; we want to reset the colors
            if (k.wVirtualKeyCode == VK_RETURN) {
                size_t ir_uibuf = 0;
                while (screenbuf[i_screenbuf++] = uibuf[ir_uibuf]) {
                    uibuf[ir_uibuf++] = '\0';
                    if (i_screenbuf >= 498) {
                        screenbuf[i_screenbuf - 1] = '\n';
                        screenbuf[i_screenbuf] = '\0';
                        i_screenbuf = 0;
                    }
                }
                screenbuf[--i_screenbuf] = '\n';
                screenbuf[++i_screenbuf] = '\0';
                i_uibuf = 0;
            }
            // TODO: else if ctrl+c exit, needed?
            // TODO: need to check union?
//            else if (k.wVirtualKeyCode < 0x30 || k.wVirtualKeyCode > 0x5A)
//                continue;
            else if (k.uChar.AsciiChar > 31 && i_uibuf < sizeof(uibuf) - 1)
                uibuf[i_uibuf++] = k.uChar.AsciiChar;
        }
        GetConsoleScreenBufferInfo(h_stdout, &csbi);

        columns = csbi.srWindow.Right - csbi.srWindow.Left + 1;
        rows = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
        printf("\033 7");
        printf("\033[0m");
        printf("\033[2J\033[4;4H%s-----ENDSCREENBUF------\n", screenbuf);
        printf("\033[%d;0H\033[48;5;27m\033[2K", rows);
        printf("\033[38;5;15m%dx%d>%s", rows, columns, uibuf);
        printf("\033[0m");
        printf("\033 8");
    }
    printf("\033[0m");
    printf("\033[2J");
    quit = true;
    return 0;
}

/*
    while (!exit) {
        INPUT_RECORD ir_in_buf[128];
        DWORD events = 0;
        ReadConsoleInput(h_stdin, ir_in_buf, 128, &events);
        for (DWORD i = 0; i < events; i++) {
            if (ir_in_buf[i].EventType == MOUSE_EVENT) {
                MOUSE_EVENT_RECORD m = ir_in_buf[i].Event.MouseEvent;
                // Process the mouse event
                printf("MOUSE EVENT!\n");
            }
            if (ir_in_buf[i].EventType == KEY_EVENT) {
                KEY_EVENT_RECORD k = ir_in_buf[i].Event.KeyEvent;
//                if (k.bKeyDown && k.wRepeatCount < 2) {
                if (k.bKeyDown) {
                    if (k.wVirtualKeyCode == VK_BACK) {
                        input_buffer[--i_input_buffer_end] = '\0';
                    } else if (k.wVirtualKeyCode == 0x53 &&
                            k.dwControlKeyState & LEFT_CTRL_PRESSED) {
                        printf("CTRL+S\n");
                        exit = true; 
                    }
                    else {
                        input_buffer[i_input_buffer_end++] = k.uChar.AsciiChar;
                    }
                    printf("key='%c', repeat=%d\n", k.uChar.AsciiChar,
                            k.wRepeatCount);
                    printf("currbuff='%s'\n", input_buffer);

                }
            }
        }
    }
    */
