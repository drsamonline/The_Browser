# Fails when the stripped aetheris binary exceeds the size budget.
#
# "Minimal" is a comparison, not an absolute: the project defines its budget as
# 10 MB, which is roughly 40x smaller than a WebKitGTK browser, 100x smaller
# than Firefox, and in the same class as NetSurf. Run with:
#   cmake -DBINARY=<path> -Dbudget=10485760 -P Tools/check_binary_size.cmake

if(NOT DEFINED BINARY OR NOT DEFINED budget)
    message(FATAL_ERROR "check_binary_size: BINARY and budget are required")
endif()

if(NOT EXISTS "${BINARY}")
    message(FATAL_ERROR "check_binary_size: no such file: ${BINARY}")
endif()

file(SIZE "${BINARY}" actual)

math(EXPR budget_kb "${budget} / 1024")
math(EXPR actual_kb "${actual} / 1024")

if(actual GREATER budget)
    message(FATAL_ERROR
        "binary size ${actual} bytes (${actual_kb} KiB) exceeds the ${budget} byte "
        "(${budget_kb} KiB) budget by ${actual} - ${budget} bytes")
endif()

message(STATUS "binary size ok: ${actual} bytes (${actual_kb} KiB) within ${budget_kb} KiB budget")
