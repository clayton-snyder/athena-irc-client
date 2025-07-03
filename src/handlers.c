#include "handlers.h"

#include "log.h"
#include "msgutils.h"
#include "screen_framework.h"
#include "stringutils.h"
#include "terminalutils.h"

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

// Max IRC message allowed is 512 including the CRLF delimiter. This macro value
// should be used when validating strlen() of messages prior to preparing them
// with CRLF delimiter. Use this macro value + 1 when allocating buffers to 
// prepare messages to pass to send_as_irc().
#define MAX_CMD_LEN 510

// Use this to allocate buffers that will be sent to the IRC server. The two 
// extra bytes are reserved for CR and LF.
#define IRC_MSG_BUF_LEN (MAX_CMD_LEN + 2)

#define CHANNEL_PREFIXES "&#+!"

// IRC message handlers
static bool handle_ircmsg_default(ircmsg *const ircm, const_str ts);
static bool handle_ircmsg_privmsg(ircmsg *const ircm, const_str ts);
static bool handle_ircmsg_topic(ircmsg *const ircm);

// Local command handlers
static void handle_localcmd_channel(char *msg, SOCKET sock);
static void handle_localcmd_join(char *msg, SOCKET sock);
static void handle_localcmd_show(char *msg);

// Screen formatters
static char s_scrbuf[SCREENMSG_BUF_SIZE] = {0};
static bool screenfmt_default(char *const buf, size_t bufsize,
       const_str src, const_str cmd, const msglist *const params, const_str ts);
static bool screenfmt_privmsg(char *const buf, size_t bufsize,
       const_str from, const_str msg, const_str ts, bool self);

// Utilities
static int send_as_irc(SOCKET sock, const char* msg);
static bool try_send_as_irc(SOCKET sock, const char* fmt, ...);


static termutils_color s_color_ts = TERMUTILS_COLOR_BLUE;
static termutils_color s_color_namebrackets = TERMUTILS_COLOR_DEFAULT;
static termutils_color s_color_text_user = TERMUTILS_COLOR_DEFAULT;
static termutils_color s_color_name_self = TERMUTILS_COLOR_CYAN_BRIGHT;
static termutils_color s_color_name_user = TERMUTILS_COLOR_YELLOW;
// static termutils_color s_color_name_op = TERMUTILS_COLOR_RED;
static termutils_color s_color_name_server = TERMUTILS_COLOR_MAGENTA;

static int s_color256_text_server = 245;

/*****************************************************************************/
/************************** IRCMSG HANDLER IMPLs *****************************/

bool handle_ircmsg(ircmsg *const ircm, const_str ts) {
    if (strcmp(ircm->command, "PRIVMSG") == 0)
        return handle_ircmsg_privmsg(ircm, ts);
    // TODO: change this to detect number and make handle_ircmsg_numeric()
    if (strcmp(ircm->command, "332") == 0 || strcmp(ircm->command, "331") == 0)
        return handle_ircmsg_topic(ircm);

    return handle_ircmsg_default(ircm, ts);
}

static bool handle_ircmsg_default(ircmsg *const ircm, const_str ts) {
    assert(ircm != NULL);
    assert(ircm->command != NULL);

    bool success = screenfmt_default(s_scrbuf, sizeof(s_scrbuf), ircm->source,
            ircm->command, &ircm->params, ts);


    if (!success) {
        log_fmt(LOGLEVEL_ERROR, "[%s] Could not screenfmt default. "
              "command='%s', param.count=%d, ts='%s'",
              "handle_ircmsg_default()", ircm->command, ircm->params.count, ts);
    }
    else {
        scrmgr_deliver_copy("home", s_scrbuf);
    }
    
    return success;
}

static bool handle_ircmsg_topic(ircmsg *const ircm) {
    assert(ircm != NULL);
    assert(ircm->command != NULL);
    assert(ircm->params.count == 3);

    // First param is client name, we don't really care?
    const_str channel = ircm->params.head->next->msg;
    const_str topic = ircm->params.tail->msg;

    return scrmgr_set_topic(channel, topic);
}

static bool handle_ircmsg_privmsg(ircmsg *const ircm, const_str ts) {
    // :source PRIVMSG <target>{,<target>} :<text>
    // TODO: debug asserts
    assert(ircm->params.count == 2);
    char *bang = strstr(ircm->source, "!");
    if (bang != NULL) *bang = '\0';

    const_str from = ircm->source;
    const_str to = ircm->params.head->msg;
    const_str msg = ircm->params.tail->msg;

    bool success = screenfmt_privmsg(
            s_scrbuf, sizeof(s_scrbuf), from, msg, ts, false);

    if (!success) {
        log_fmt(LOGLEVEL_ERROR, "[%s] Could not screenfmt privmsg. from='%s', "
             "msg='%s', ts='%s'", "handle_ircmsg_privmsg()", from, to, msg, ts);
    }
    else {
        scrmgr_deliver_copy(to, s_scrbuf);
    }
    
    if (bang != NULL) *bang = '!'; // TODO: do we really need this?
    return success;
}



/*****************************************************************************/
/************************** SCREEN FORMAT IMPLs ******************************/

static bool screenfmt_default(char *const buf, size_t bufsize,
        const_str src, const_str cmd, const msglist *const params, const_str ts)
{
    size_t n_bytes = 0;
    n_bytes += termutils_set_text_color_buf(buf, bufsize, s_color_ts);

    int bytes = sprintf_s(buf + n_bytes, bufsize - n_bytes, "[%s] ", ts);
    if (bytes < 0) return false;
    n_bytes += bytes;
    
    n_bytes += termutils_set_text_color_buf(
            buf + n_bytes, bufsize - n_bytes, s_color_name_server);
    if (n_bytes >= bufsize) return false;
    bytes = sprintf_s(buf + n_bytes, bufsize - n_bytes, "%s: ", src);
    if (bytes < 0) return false;
    n_bytes += bytes;

    bytes = termutils_set_text_color_256_buf(
            buf + n_bytes, bufsize - n_bytes, s_color256_text_server);
    if (bytes < 0) return false;
    n_bytes += bytes;
    if (n_bytes >= bufsize) return false;

    bytes = sprintf_s(buf + n_bytes, bufsize - n_bytes, "(%s)", cmd);
    if (bytes < 0) return false;
    n_bytes += bytes;

    struct msgnode *curr = params->head;
    while (curr != NULL) {
        bytes = sprintf_s(buf + n_bytes, bufsize - n_bytes, " %s", curr->msg);
        if (bytes < 0) return false;
        n_bytes += bytes;
        curr = curr->next;
    }

    n_bytes += termutils_reset_text_color_buf(buf + n_bytes, bufsize - n_bytes);
    return n_bytes <= bufsize;
}

static bool screenfmt_privmsg(char *const buf, size_t bufsize,
        const_str from, const_str msg, const_str ts, bool self)
{
    termutils_color color_name = self ? s_color_name_self : s_color_name_user;
    // The size checking is strange here, but avoids a massive function. Since
    // the termutils functions and sprintf_s() won't write anything that can't
    // fit, we can plow through those and only check n_bytes before we write a
    // character manually. We avoid adding -1 (from sprintf_s() failure) to
    // n_bytes by simply returning false if we ever see it.
    size_t n_bytes = 0;
    n_bytes += termutils_set_text_color_buf(buf, bufsize, s_color_ts);

    int bytes = sprintf_s(buf + n_bytes, bufsize - n_bytes, "[%s] ", ts);
    if (bytes < 0) return false;
    n_bytes += bytes;
    
    n_bytes += termutils_set_text_color_buf(
            buf + n_bytes, bufsize - n_bytes, s_color_namebrackets);
    if (n_bytes >= bufsize) return false;
    buf[n_bytes++] = '<';

    n_bytes += termutils_set_bold_buf(buf + n_bytes, bufsize - n_bytes, self);
    n_bytes += termutils_set_text_color_buf(
            buf + n_bytes, bufsize - n_bytes, color_name);
    bytes = sprintf_s(buf + n_bytes, bufsize - n_bytes, "%s", from);
    if (bytes < 0) return false;
    n_bytes += bytes;

    n_bytes += termutils_set_bold_buf(buf + n_bytes, bufsize - n_bytes, false);
    n_bytes += termutils_set_text_color_buf(
            buf + n_bytes, bufsize - n_bytes, s_color_namebrackets);
    if (n_bytes >= bufsize) return false;
    buf[n_bytes++] = '>';
    n_bytes += termutils_set_text_color_buf(
            buf + n_bytes, bufsize - n_bytes, s_color_text_user);
    bytes = sprintf_s(buf + n_bytes, bufsize - n_bytes, " %s", msg);
    if (bytes < 0) return false;
    n_bytes += bytes;
    n_bytes += termutils_reset_text_color_buf(buf + n_bytes, bufsize - n_bytes);

    return n_bytes <= bufsize;
}

static bool screenfmt_privmsg_error(char *const buf, size_t bufsize,
        const_str error_msg, const_str from, const_str msg, const_str ts)
{
    size_t n_bytes = 
        termutils_set_text_color_buf(buf, bufsize, TERMUTILS_COLOR_RED);
    int bytes = sprintf_s(buf + n_bytes, bufsize - n_bytes, "(%s) ", error_msg);
    if (bytes < 0) return false;
    n_bytes += bytes;

    n_bytes += termutils_set_text_color_256_buf(buf, bufsize, 245);
    bytes = sprintf_s(buf + n_bytes, bufsize - n_bytes, "[%s] <%s> %s",
            ts, from, msg);
    if (bytes < 0) return false;

    return true;
}


/*****************************************************************************/
/************************* LOCALCMD HANDLER IMPLs ****************************/

// Returns true if program should exit.
// TODO: Remove nick param. Access from a query or state struct when needed
bool handle_user_command(char *msg, const_str nick, SOCKET sock, const_str ts) {
    assert(msg != NULL);
    assert(sock != INVALID_SOCKET);
    log_fmt(LOGLEVEL_DEV, "[handle_user_command()] Processing '%s'", msg);
    
    if (strcmp(msg, "!quit") == 0) {
        try_send_as_irc(sock, "QUIT");
        scrmgr_deliver_copy("home", "Disconnecting...");
        return true;
    }

    switch (msg[0]) {
    case '!':
        // TODO: Do we want to send this to a screenlog?
        // ! indicates an internal client command.
        if (strut_startswith(msg, "!channel ") || strcmp(msg, "!channel") == 0)
            handle_localcmd_channel(msg, sock);
        if (strut_startswith(msg, "!join ") || strcmp(msg, "!join") == 0)
            handle_localcmd_join(msg, sock);
        if (strut_startswith(msg, "!show ") || strcmp(msg, "!show") == 0)
            handle_localcmd_show(msg);
        break;
    case '`':
        // TODO: Do we want to send this to a screenlog?
        // ` is for sending the message to the server unmodified.
        try_send_as_irc(sock, (msg + 1));
        break;
    default:
        // No prefix means a chat message to the active channel.
        // TODO: change format depending on the screen. Channels get PRIVMSG,
        // home screen gets... idk, channel list can be formatted into a JOIN
        // (e.g., user enters a number and we select the channel name and try
        // to JOIN it).
        // if active_screen is a CHANNEL...
        // TODO: send to active_screen screenlog
        const_str active_name = scrmgr_get_active_name();
        bool send_success = try_send_as_irc(
                sock, "PRIVMSG %s :%s", active_name, msg);
        
        bool fmt_success = false;
        if (send_success)
            fmt_success = screenfmt_privmsg(s_scrbuf, sizeof(s_scrbuf),
                                nick, msg, ts, true);
        else
            fmt_success = screenfmt_privmsg_error(s_scrbuf, sizeof(s_scrbuf),
                                "Not Sent", nick, msg, ts);

        if (!fmt_success) {
            log_fmt(LOGLEVEL_ERROR, "[%s] Couldn't screenfmt privmsg. "
                    "nick='%s', msg='%s', ts='%s'", "handle_ircmsg_privmsg()",
                    nick, msg, ts);
            scrmgr_deliver_copy(active_name, "<corrupted message>");
        }
        else {
            scrmgr_deliver_copy(active_name, s_scrbuf);
        }
    }
    return false;
}

static void handle_localcmd_channel(char *msg, SOCKET sock) {
    assert(msg != NULL);
    assert(sock != INVALID_SOCKET);

    // These are used internally by strtok_s()
    // TODO: use this when implementing x-plat: rsize_t strmax = sizeof(msg);
    const char* delim = " ";
    char *next_tk = NULL;

    // TODO: This is the Microsoft version. Of course it's got a different
    // contract than the C11 one. To make this cross-plat, you need to define a
    // macro that also takes `rsize *strmax` (initialized to sizeof(msg)) and
    // passes that each time if not on MSVC.
    char* tk_cmd = strtok_s(msg, delim, &next_tk);
    assert(strcmp(tk_cmd, "!channel") == 0);
    char* tk_action = strtok_s(NULL, delim, &next_tk);
    if (tk_action == NULL || strcmp(tk_action, "help") == 0) {
        // TODO: output
        termutils_set_text_color(TERMUTILS_COLOR_YELLOW, stdout);
        // TODO: HELP string
        puts("Usage: !channel <action> [args...]");
        puts("Avaiable actions for !channel:\n"
                "* list [names]\n"
                "\tDisplay info about the listed channels."
                "\n\t[names] is a comma-separated list (with NO spaces)."
                "\n\tIf [names] is omitted, lists all channels.\n"
                "* join <name>\n"
                "\tJoin or create the specified channel.\n"
                "* switch <name>\n"
                "\tSwitch the active (displayed) channel to the one specified."
                "\n\tYou must have previously joined the channel.\n"
                "* part <name>\n"
                "\tLeave the specified channel.\n");
    }
    else if (strcmp(tk_action, "list") == 0) {
        char* tk_names = strtok_s(NULL, delim, &next_tk);
        if (tk_names != NULL &&
            strtok_s(NULL, delim, &next_tk) != NULL)
        {
            // TODO: output
            termutils_set_text_color(TERMUTILS_COLOR_YELLOW, stdout);
            termutils_set_bold(true, stdout);
            puts("Too many arguments for 'list' command. [names] should be "
                    "a comma-separated list (with no spaces) of the channel"
                    " names you wish to display info about.");
            termutils_reset_all(stdout);
        }
        try_send_as_irc(sock, "LIST %s", tk_names ? tk_names : "");
    }
    // TODO: other !channel actions
    log_fmt(LOGLEVEL_DEV, "Performed !channel %s", tk_action ? tk_action : "");
}

static void handle_localcmd_show(char *msg) {
    assert(msg != NULL);

    const_str delim = " ";
    char *next_tk;
    const_str tk_cmd = strtok_s(msg, delim, &next_tk);
    assert(strcmp(tk_cmd, "!show") == 0 || strcmp(tk_cmd, "!s") == 0);
    const_str tk_screen = strtok_s(NULL, delim, &next_tk);
    if (tk_screen == NULL) {
        // TODO: command feedback
        return;
    }

    size_t tk_screen_len = strlen(tk_screen);
    if (tk_screen_len == 0) {
        // TODO: command feedback
        return;
    }
    else if (tk_screen_len == 1 && isdigit(tk_screen[0])) {
        int i_scr = strut_ctoi(tk_screen[0]);
        assert(i_scr >= 0 && i_scr <= 9);
        if (!scrmgr_show_index(i_scr)) {
            // TODO: command feedback
            log_fmt(LOGLEVEL_WARNING, "'!show %s' did nothing because there is "
                    "no screen %d.", tk_screen, i_scr);
        }
        return;
    }

    if (!scrmgr_show_name_startswith(tk_screen)) {
        // TODO: command feedback
        log_fmt(LOGLEVEL_WARNING, "'!show %s' did nothing because no screen "
                "whose name starts with '%s' exists.", tk_screen, tk_screen);
    }
}

static void handle_localcmd_join(char *msg, SOCKET sock) {
    assert(msg != NULL);
    assert(sock != INVALID_SOCKET);

    const_str delim = " ";
    char *next_tk;
    char *const tk_cmd = strtok_s(msg, delim, &next_tk);
    assert(strcmp(tk_cmd, "!join") == 0);
    char *const tk_channel = strtok_s(NULL, delim, &next_tk);
    if (tk_channel == NULL) {
        // TODO: command feedback channel?
        return;
    }
    if (strchr(CHANNEL_PREFIXES, tk_channel[0]) == NULL) {
        // TODO: command feedback channel?
        return;
    }
    size_t tk_channel_len = strlen(tk_channel);
    if (tk_channel_len > CHANNEL_NAME_MAXLEN) {
        // TODO: command feedback channel?
        return;
    }
    // TODO: check for invalid chars
    bool sent = try_send_as_irc(sock, "JOIN %s", tk_channel);
    if (!sent) {
        log(LOGLEVEL_ERROR, "[handle_localcmd_join] try_send_as_irc() failed.");
        return;
    }
    scrmgr_create_or_switch(tk_channel);
}

/*****************************************************************************/
/***************************** UTIL IMPLs ************************************/

static int send_as_irc(SOCKET sock, const char* msg) {
    // We need to replace \0 with \r\n
    size_t msg_len = strlen(msg) + 2;
    assert(msg_len <= IRC_MSG_BUF_LEN);
    char send_buff[IRC_MSG_BUF_LEN];
    strcpy_s(send_buff, sizeof(send_buff), msg);
    send_buff[msg_len - 2] = '\r';
    send_buff[msg_len - 1] = '\n';

    return send(sock, send_buff, msg_len, 0);
}

static bool try_send_as_irc(SOCKET sock, const char* fmt, ...) {
    va_list fmt_args;
    va_start(fmt_args, fmt);
    // vsnprintf writes up to bufsz - 1 characters, plus the null term. Ergo
    // MAX_CMD_LEN + 1 gives us room for CRLF if we overwrite the null term.
    char irc_cmd[MAX_CMD_LEN + 1];
    int full_len = vsnprintf(irc_cmd, sizeof(irc_cmd), fmt, fmt_args);
    va_end(fmt_args);

    // vsnprintf returns the would-be expanded string length (without the null
    // term), so we can use it to check for truncation.
    if (full_len + 2 > IRC_MSG_BUF_LEN) {
        log_fmt(LOGLEVEL_WARNING, "[main] Expanded message too long (%d); not "
                "sent: \'%s\'(truncated)", full_len, irc_cmd);
        //TODO: OUTPUT
        termutils_set_text_color(TERMUTILS_COLOR_YELLOW, stdout);
        termutils_set_bold(true, stdout);
        printf("Your message is too long. Reduce it by %d characters.\n",
                full_len + 2 - IRC_MSG_BUF_LEN);
        termutils_reset_all(stdout);
        return false;
    }

    int result = send_as_irc(sock, irc_cmd);
    if (result == SOCKET_ERROR) {
        // TODO: communicate failure
        log_fmt(LOGLEVEL_ERROR, "[try_send_as_irc] send() failed: %lu",
                WSAGetLastError());
        return false;
    }
    return true;
}
