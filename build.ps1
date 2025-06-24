# Must be run from a Developer PS/cmd session. Run Launch-VsDevShell.ps1 first.

param ( 
    [switch] $AllowWarnings,
    [switch] $LeaveOldArtifacts
)

$output_dir = "built"
$exe_filename = "athena-irc.exe";

if (-not(test-path "$output_dir")) {
    mkdir "$output_dir";
}

if ($LeaveOldArtifacts) {
    Write-Host "LeaveOldArtifacts enabled; not clearing old output files first." -Foreground Yellow
} else {
    rm "$output_dir\*.obj";
    rm "$output_dir\*.exe";
}

$warnings_as_errors = "/WX";

if ($AllowWarnings) {
    Write-Host "WARNING: AllowWarnings enabled; don't leave this on!" -Foreground DarkYellow 
    $warnings_as_errors = "";
}

# Disabled warnings:
# * [4820] Padding inserted after data member (winsock2.h causes these).
# * [5045] Notes where compiler may insert Qspectre protection (potential perf cost).
#cl main.c msgqueue.c log.c terminalutils.c stringutils.c screen_framework.c msgutils.c handlers.c ws2_32.lib /Qspectre /DWIN32_LEAN_AND_MEAN /DTERMUTILS_DEBUG_ASSERT /Wall /wd4820 /wd5045 $warnings_as_errors /Fe: "$outfilename";
cl src\*.c ws2_32.lib /I"include" /Qspectre /DWIN32_LEAN_AND_MEAN /DTERMUTILS_DEBUG_ASSERT /Wall /wd4820 /wd5045 $warnings_as_errors /Fo"$output_dir\" /Fe"$output_dir\$exe_filename";
#/link /out:"$output_dir\$exe_filename";
