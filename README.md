> [!NOTE]
> WIP

# AthenaIRC Client

AthenaIRC is a console-based IRC client with a custom TUI (text user interface)
for managing server connections, organizing channels, displaying ANSI-formatted
text, and of course chatting. It is being implemented against the
[Modern IRC Client Protocol](https://modern.ircdocs.horse/).

## User Interface
AthenaIRC manually draws its custom interface to the alternative terminal 
buffer and restores your previous session upon exit. Drawing is done using
[ANSI escape sequences](https://en.wikipedia.org/wiki/ANSI_escape_code), a 
ubiquitous standard for in-band signaling of cursor position, foreground and
background color, text formatting, saving/erasing/restoring of lines and
screens, and more. [This gist](https://gist.github.com/fnky/458719343aabd01cfb17a3a4f7296797)
provides a good overview of the options.

In addition to enabling the client's TUI formatting, ANSI escapes are used to
implement the in-band [IRC formatting protocol](https://modern.ircdocs.horse/formatting).

## Platforms
<WIP> I'm developing the initial version for Windows with full intent to port
it to Linux when all the basic features are done. I was more nervous about 
implementing the Windows version, so I started there.

Thanks to the progress and direction of the new(ish) Windows Terminal, ANSI
escape sequences can be considered truly cross-platform. To that end, all
formatting is done manually and without the use of a curses-like library.

#### Windows Console Host
What's often referred to as the "classic Windows Console" is really any console
program being hosted by the Windows Console Host (i.e., `conhost.exe`). Until
recently, this was the default host when launching Command Prompt or Powershell.
It's gradually being phased out in favor of Windows Terminal, which is already
the default on Windows 11. AthenaIRC won't work on the old Windows Console
Host (worse - it will work and spam nonsense to the screen with such a speed
that reading it is impossible).


## Using the Client
Messages prefixed with `!` are interpreted as client commands.
Messages prefixed with `\` are interpreted as literal IRC messages and will be
sent directly, without modification, *except* for replacing the null terminator
with the IRC message delimiter (`\\r\\n`).
Messages with no prefix are interpreted as an attempt to send a message to the
current channel. 

