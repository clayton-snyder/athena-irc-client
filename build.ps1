# Must be run from a Developer PS/cmd session. Run Launch-VsDevShell.ps1 first.

rm *.obj;

$outfilename = "client.exe";

# Makes it impossible to run the program when build fails because I do do that.
if (test-path "$outfilename") {
    rm "$outfilename";
}

# Disabled warnings:
# * [4820] Padding inserted after data member (winsock2.h causes these).
# * [5045] Notes where compiler may insert Qspectre protection (potential perf cost).
cl main.c msgqueue.c log.c terminalutils.c stringutils.c channel.c ws2_32.lib /Qspectre /DWIN32_LEAN_AND_MEAN /DTERMUTILS_DEBUG_ASSERT /Wall /wd4820 /wd5045 /WX /Fe: "$outfilename";
