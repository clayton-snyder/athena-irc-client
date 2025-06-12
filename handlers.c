#include "handlers.h"

#include "log.h"
#include "msgutils.h"
#include "screen_framework.h"
#include "terminalutils.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static char s_scrbuf[SCREENMSG_BUF_SIZE] = {0};
static bool handle_privmsg(ircmsg *const ircm, const_str ts);

static bool screenfmt_privmsg(char *const buf, size_t bufsize,
        const_str from, const_str msg, const_str ts);

bool handle_ircmsg(ircmsg *const ircm, const_str ts) {
    if (strcmp(ircm->command, "PRIVMSG") == 0)
        return handle_privmsg(ircm, ts);
    return false;
}

static bool handle_privmsg(ircmsg *const ircm, const_str ts) {
    // :source PRIVMSG <target>{,<target>} :<text>
    // TODO: debug asserts
    assert(ircm->params.count == 2);
    char *bang = strstr(ircm->source, "!");
    if (bang != NULL) *bang = '\0';

    const_str from = ircm->source;
    const_str to = ircm->params.head->msg;
    const_str msg = ircm->params.tail->msg;

    bool success = screenfmt_privmsg(s_scrbuf, sizeof(s_scrbuf), from, msg, ts);

    if (!success) {
        log_fmt(LOGLEVEL_ERROR, "[%s] Could not screenfmt message. from='%s', "
                "msg='%s', ts='%s'", "handle_privmsg()", from, to, msg, ts);
    }
    else {
        // TODO: Actually you need to find the appropriate screen based on the
        // source or "to" param. Probably need a screen_search option.
        screen_pushmsg_copy(screen_getid_active(), s_scrbuf);       
    }
    
    if (bang != NULL) *bang = '!'; // TODO: do we really need this?
    return success;
}


static bool screenfmt_privmsg(char *const buf, size_t bufsize,
        const_str from, const_str msg, const_str ts)
{
    // The size checking is strange here, but avoids a massive function. Since
    // the termutils functions and sprintf_s() won't write anything that can't
    // fit, we can plow through those and only check n_bytes before we write a
    // character manually. We avoid adding -1 (from sprintf_s() failure) to
    // n_bytes by simply returning false if we ever see it.
    size_t n_bytes = 0;
    n_bytes += termutils_set_text_color_buf(buf, bufsize, TERMUTILS_COLOR_BLUE);

    int bytes = sprintf_s(buf + n_bytes, bufsize - n_bytes, "[%s] ", ts);
    if (bytes < 0) return false;
    n_bytes += bytes;
    
    n_bytes += termutils_set_text_color_buf(buf + n_bytes, bufsize - n_bytes, 
            TERMUTILS_COLOR_YELLOW);
    if (n_bytes >= bufsize) return false;
    buf[n_bytes++] = '<';

    n_bytes += termutils_set_bold_buf(buf + n_bytes, bufsize - n_bytes, true);
    bytes = sprintf_s(buf + n_bytes, bufsize - n_bytes, "%s", from);
    if (bytes < 0) return false;
    n_bytes += bytes;

    n_bytes += termutils_set_bold_buf(buf + n_bytes, bufsize - n_bytes, false);
    if (n_bytes >= bufsize) return false;
    buf[n_bytes++] = '>';
    n_bytes += termutils_reset_text_color_buf(buf + n_bytes, bufsize - n_bytes);

    bytes = sprintf_s(buf + n_bytes, bufsize - n_bytes, " %s", msg);
    if (bytes < 0) return false;

    return true;
}
