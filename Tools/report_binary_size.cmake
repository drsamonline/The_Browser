# Reports the stripped aetheris binary size. This NEVER fails the build.
#
# The browser is expected to grow as functionality lands, so size is a signal to
# watch rather than a gate to pass: "keep it as small as possible", not "stay
# under a limit". Compare the reported figure against the reference below and
# against other browsers (Firefox 238-428 MB, Chromium 280-447 MB, WebKitGTK
# ~95 MB, NetSurf 4.0-6.9 MB, Dillo ~0.25 MB without JavaScript).
#
# Usage:
#   cmake -DBINARY=<path> [-Dreference=<bytes>] -P Tools/report_binary_size.cmake

if(NOT DEFINED BINARY)
    message(STATUS "size report: no BINARY given, nothing to report")
    return()
endif()

if(NOT EXISTS "${BINARY}")
    message(STATUS "size report: ${BINARY} does not exist yet, nothing to report")
    return()
endif()

file(SIZE "${BINARY}" actual)
math(EXPR actual_kb "${actual} / 1024")
math(EXPR actual_kib_whole "${actual} / 1024")

set(line "size report: ${actual} bytes (${actual_kib_whole} KiB)")

if(DEFINED reference AND reference GREATER 0)
    math(EXPR delta "${actual} - ${reference}")
    if(delta GREATER 0)
        math(EXPR delta_kb "${delta} / 1024")
        string(APPEND line " — +${delta} bytes (+${delta_kb} KiB) since the last reference")
    elseif(delta LESS 0)
        math(EXPR shrink "${reference} - ${actual}")
        string(APPEND line " — ${shrink} bytes smaller than the last reference")
    else()
        string(APPEND line " — unchanged since the last reference")
    endif()
endif()

message(STATUS "${line}")
