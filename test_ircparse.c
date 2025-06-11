#include "log.h"
#include "msgutils.h"

#include <stdio.h>


int main(void) {
    set_logger_level(LOGLEVEL_WARNING);

    char str_a[] = ":source.hello.hi COMMAND i command you :to parse this!";
    printf("rawmsg: '%s'\n", str_a);
    ircmsg *ircm = msgutils_ircmsg_parse(str_a);
    DEBUG_ircmsg_print(ircm);
    printf("\n");

    char str_a2[] = ":source.hello.hey TEST :Only a colon param!!";
    printf("rawmsg: '%s'\n", str_a2);
    ircm = msgutils_ircmsg_parse(str_a2);
    DEBUG_ircmsg_print(ircm);
    printf("\n");

    char str_b[] = "C";
    printf("rawmsg: '%s'\n", str_b);
    ircm = msgutils_ircmsg_parse(str_b);
    DEBUG_ircmsg_print(ircm);
    printf("\n");
    
    char str_c[] = "  COMMAND    :he l l o o o#@ f3h289fHd :   \0";
    printf("rawmsg: '%s'\n", str_c);
    ircm = msgutils_ircmsg_parse(str_c);
    DEBUG_ircmsg_print(ircm);
    printf("\n");
    
    char str_d[] = ":\\ weirdsource 1 param 3: :";
    printf("rawmsg: '%s'\n", str_d);
    ircm = msgutils_ircmsg_parse(str_d);
    DEBUG_ircmsg_print(ircm);
    printf("\n");
    
    char str_e[] = ":::source:with:colons: : : :";
    printf("rawmsg: '%s'\n", str_e);
    ircm = msgutils_ircmsg_parse(str_e);
    DEBUG_ircmsg_print(ircm);
    printf("\n");

    return 0;
}
