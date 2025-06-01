# Must be run from a Developer PS/cmd session. Run Launch-VsDevShell.ps1 first.
cl main.c msgqueue.c log.c terminalutils.c stringutils.c ws2_32.lib /DWIN32_LEAN_AND_MEAN /DTERMUTILS_DEBUG_ASSERT /Fe: client.exe
