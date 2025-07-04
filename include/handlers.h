#pragma once

#include "athena_types.h"
#include "msgutils.h"

#include <stdbool.h>
#include <winsock2.h>

// Dispatches to a specific ircmsg handler by reading 'ircm->command'.
bool handle_ircmsg(ircmsg *const ircm, const_str ts); 

// Dispatches to a specific localcmd handler by reading 'cmd_str'.
bool handle_user_command(char *msg, const_str nick, SOCKET sock, const_str ts);
