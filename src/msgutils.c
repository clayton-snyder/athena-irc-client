#include "log.h"
#include "msgqueue.h"
#include "msgutils.h"
#include "terminalutils.h"

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

// static bool is_ALPHA(char c) {
//     return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
// }
// 
// static bool is_DIGIT(char c) {
//     return c >= '0' && c <= '9';
// }

// Prints a log error with provided prefix, stage, and whatever raw msg string 
// can be provided, and frees the provided ircmsg. 
static void handle_bad_format(const char *rawmsg, const char *stage,
        ircmsg *new_ircmsg, const char* logpfx);

static void handle_bad_format(const char *rawmsg, const char *stage,
        ircmsg *new_ircmsg, const char* logpfx) 
{
    log_fmt(LOGLEVEL_ERROR, "[%s] Could not parse message as valid IRC (failed "
            "when parsing %s): '%s'", logpfx, stage, rawmsg);
    // TODO: debug assert
    assert(!"Could not parse message as valid IRC.");
    msgutils_ircmsg_free(new_ircmsg);
}

// bool msgutils_ircmsg_validate(ircmsg *ircm) {
// TODO: GENERAL validation; is the text in each field allowed by the grammar
//       NOT evaluating if the logic makes sense given state, matching params to
//       commands, etc.
//      // Params can't have colons in them except the last
//      // Command is real
// }

ircmsg* msgutils_ircmsg_parse(char *rawmsg) {
    const_str logpfx = "[msgutils_ircmsg_parse()]";
    log_fmt(LOGLEVEL_SPAM, "%s Attempting to parse: '%s'", logpfx, rawmsg);

    ircmsg *new_ircmsg = (ircmsg *) calloc(1, sizeof(ircmsg));
    if (new_ircmsg == NULL) {
        log_fmt(LOGLEVEL_ERROR, "%s OOM: calloc() ircmsg", logpfx);
        return NULL;
    }

    bool has_source = rawmsg[0] == ':';
    char *next_tk = NULL;
    size_t i_rawmsg = 0;

    if (has_source) {
        if (rawmsg[i_rawmsg + 1] == '\0' || rawmsg[i_rawmsg + 1] == ' ') {
            handle_bad_format(rawmsg, "source", new_ircmsg, logpfx);
            return NULL;
        }

        // TODO: platform-specific code; MSFT strtok_s (strcpy_s too?)
        char *tk_source = strtok_s(rawmsg + 1, " ", &next_tk);
        if (tk_source != NULL) {
            size_t dest_size = strlen(tk_source) + 1;
            new_ircmsg->source = (char *) malloc(dest_size);
            strcpy_s(new_ircmsg->source, dest_size, tk_source);
            log_fmt(LOGLEVEL_SPAM, "%s Parsed source: '%s'", logpfx,
                    new_ircmsg->source);
        }
    }

    char *tk_cmd = strtok_s(has_source ? NULL : rawmsg, " ", &next_tk);
    if (tk_cmd == NULL) {
        handle_bad_format(next_tk, "command", new_ircmsg, logpfx);
        return NULL;
    }
    size_t dest_size = strlen(tk_cmd) + 1;
    new_ircmsg->command = (char *) malloc(dest_size);
    strcpy_s(new_ircmsg->command, dest_size, tk_cmd);
    log_fmt(LOGLEVEL_SPAM,
            "%s Parsed command: '%s'", logpfx, new_ircmsg->command);

    // A ':' in the parameters section indicates the final parameter that
    // extends to the end of the message (i.e., it can contain spaces).
    // If it exists, we'll mark the ':' as \0, parse the space-delimited params
    // up to there, then append the final param.
    char *tk_colon_param = NULL;
    // The final param ':' has to have a preceding space, but if it's the only
    // param, we've already overwritten that, so check manually.
    if (*next_tk == ':') {
        tk_colon_param = next_tk;
        *tk_colon_param++ = '\0';
    }
    else {
        tk_colon_param = strstr(next_tk, " :");
        if (tk_colon_param != NULL) {
            tk_colon_param++; // point at the ':'
            *tk_colon_param++ = '\0';
        }
    }

    char *tk_param = strtok_s(NULL, " ", &next_tk); 
    while (tk_param != NULL) {
        // add param
        msglist_pushback_copy(&new_ircmsg->params, tk_param);
        log_fmt(LOGLEVEL_SPAM, "%s Parsed param: '%s'", logpfx, tk_param);
        tk_param = strtok_s(NULL, " ", &next_tk);
    }

    if (tk_colon_param != NULL) {
        msglist_pushback_copy(&new_ircmsg->params, tk_colon_param);
        log_fmt(LOGLEVEL_SPAM, "%s Parsed param: '%s'", logpfx, tk_colon_param);
    }

    log_fmt(LOGLEVEL_SPAM, "%s Finished parsing ircmsg.", logpfx);

    return new_ircmsg;
}

void msgutils_ircmsg_free(ircmsg *ircm) {
    // TODO: make sure to free any new fields added to ircmsg
    free(ircm->source);
    ircm->source = NULL;

    free(ircm->command);
    ircm->command = NULL;

    msglist_free(&ircm->params);

    free(ircm);
}

bool msgutils_get_timestamp(
        char *const buf, size_t bufsize, bool utc, timestamp_format format)
{
    time_t time_now = time(NULL);
    struct tm tm_now;
    // TODO: platform-specific. Microsoft swaps the order of these parameters.
    // And returns errno_t instead of tm*. Thank you Microsoft! Very cool!
    errno_t err =
        utc ? gmtime_s(&tm_now, &time_now) : localtime_s(&tm_now, &time_now);
    // Normally: struct tm *ptm = gmtime_s(&time_now, &tm_now);

    // if (ptm_now == NULL) {
    if (err != 0) {
        log_fmt(LOGLEVEL_ERROR, "[get_timestamp()] %s returned %d.", 
                utc ? "gmtime_s()" : "localtime_s()", err);
        return false;
    }

    switch(format) {
    case TIMESTAMP_FORMAT_YEAR_MONTH_DAY_TIME:
        strftime(buf, bufsize, "%Y-%m-%d %H:%M:%S", &tm_now);
        break;
    case TIMESTAMP_FORMAT_MONTH_DAY_TIME:
        strftime(buf, bufsize, "%m-%d %H:%M:%S", &tm_now);
        break;
    case TIMESTAMP_FORMAT_TIME_ONLY:
        strftime(buf, bufsize, "%H:%M:%S", &tm_now);
        break;
    case TIMESTAMP_FORMAT_YEAR_MONTH_DAY_NOTIME:
        strftime(buf, bufsize, "%Y-%m-%d", &tm_now);
        break;
    case TIMESTAMP_FORMAT_MONTH_DAY_NOTIME:
        strftime(buf, bufsize, "%m-%d", &tm_now);
        break;
    default:
        log_fmt(LOGLEVEL_ERROR, "Invalid timestamp_format: %d", format);
        assert(!"Invalid timestamp_format! See error log.");
        return false;
    }

    return true;
}

