<WIP>

# Using the Client
Messages prefixed with `!` are interpreted as client commands.
Messages prefixed with `\`` are interpreted as literal IRC messages and will be
sent directly, without modification *except for* replacing the null terminator
with `\\r\\n`, the IRC message delimiter.
Messages with no prefix are interpreted as an attempt to send a message to the
user's current channel. 

### Message Length
IRC messages have a maximum length of 512 (see [RFC 1459 section 2.3](https://www.rfc-editor.org/rfc/rfc1459#section-2.3).
Because this inlcudes the command, all parameters, and the CRLF delimiter, it's
impossible to provide an exact user input limit prior to parsing the user's 
message. If the expanded IRC message string exceeds a length of 512, the message
is simply not sent and the user is notified.

This program uses [ANSI escape codes](https://en.wikipedia.org/wiki/ANSI_escape_code)
to format terminal output. While all modern terminal emulators support these
(including Windows Terminal), the classic Windows Console Host does not, and I'm
not planning to implement a translation layer to support it. Thankfully, the
classic Windows Console is gradually being phased out in favor of Windows
Terminal, which is already the default terminal program in Windows 11.

