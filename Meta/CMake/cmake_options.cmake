option(BUILD_SHARED_LIBS "Build shared libraries instead of static libraries" ON)

option(ENABLE_COMPILETIME_FORMAT_CHECK "Enable compiletime format string checks" ${AETHERIS_ENABLE_COMPILETIME_FORMAT_CHECK})
option(ENABLE_UNDEFINED_SANITIZER "Enable undefined behavior sanitizer testing in gcc/clang" OFF)
option(UNDEFINED_BEHAVIOR_IS_FATAL "Make undefined behavior sanitizer errors non-recoverable" OFF)

option(ENABLE_ALL_THE_DEBUG_MACROS "Enable all debug macros to validate they still compile" OFF)

# Aetheris build profile. Production defaults are minimal; full optional components
# can still be enabled explicitly for development or compatibility testing.
option(AETHERIS_MINIMAL_BUILD "Enable the optimized Aetheris production build profile" ON)
if (AETHERIS_MINIMAL_BUILD)
    set(AETHERIS_OPTIONAL_COMPONENTS_DEFAULT OFF)
else()
    set(AETHERIS_OPTIONAL_COMPONENTS_DEFAULT ON)
endif()

option(AETHERIS_BUILD_WEBDRIVER "Build the WebDriver automation executable" ${AETHERIS_OPTIONAL_COMPONENTS_DEFAULT})
option(AETHERIS_BUILD_LIBDIFF "Build the LibDiff utility library" ${AETHERIS_OPTIONAL_COMPONENTS_DEFAULT})
option(AETHERIS_ENABLE_PROCESS_MANAGER_UI "Enable the internal Task Manager WebUI" ${AETHERIS_OPTIONAL_COMPONENTS_DEFAULT})
option(AETHERIS_ENABLE_BOOKMARKS_UI "Enable the internal Bookmarks WebUI page" ${AETHERIS_OPTIONAL_COMPONENTS_DEFAULT})
option(AETHERIS_ENABLE_DOWNLOADS_UI "Enable the internal Downloads WebUI page" ${AETHERIS_OPTIONAL_COMPONENTS_DEFAULT})
option(AETHERIS_ENABLE_HISTORY_UI "Enable the internal History WebUI page" ${AETHERIS_OPTIONAL_COMPONENTS_DEFAULT})
option(AETHERIS_ENABLE_DEVTOOLS "Enable browser developer tools integration" ${AETHERIS_OPTIONAL_COMPONENTS_DEFAULT})
option(AETHERIS_ENABLE_GEOLOCATION "Enable Qt geolocation provider integration" ${AETHERIS_OPTIONAL_COMPONENTS_DEFAULT})
option(AETHERIS_ENABLE_BOOKMARKS_BAR "Enable the browser bookmarks bar UI" ${AETHERIS_OPTIONAL_COMPONENTS_DEFAULT})
option(AETHERIS_ENABLE_FIND_IN_PAGE "Enable the find-in-page widget" ${AETHERIS_OPTIONAL_COMPONENTS_DEFAULT})
option(AETHERIS_BUILD_UTILITIES "Build developer and diagnostic utilities" ${AETHERIS_OPTIONAL_COMPONENTS_DEFAULT})
option(AETHERIS_ENABLE_INSTALL_RULES "Generate install/package rules" ${AETHERIS_OPTIONAL_COMPONENTS_DEFAULT})
option(BUILD_TESTING "Build the testing tree" ${AETHERIS_OPTIONAL_COMPONENTS_DEFAULT})
option(AETHERIS_ENABLE_COMPILETIME_FORMAT_CHECK "Enable compile-time format validation" ${AETHERIS_OPTIONAL_COMPONENTS_DEFAULT})
option(AETHERIS_ENABLE_INSTALL_HEADERS "Generate header installation rules" ${AETHERIS_OPTIONAL_COMPONENTS_DEFAULT})

option(AETHERIS_ENABLE_CLANG_PLUGIN_GRAPH "Attach Clang plugin dependency targets to the production build graph" ${AETHERIS_OPTIONAL_COMPONENTS_DEFAULT})
option(AETHERIS_ENABLE_LINT_TARGETS "Create developer lint and style targets" ${AETHERIS_OPTIONAL_COMPONENTS_DEFAULT})

option(INCLUDE_WASM_SPEC_TESTS "Download and include the WebAssembly spec testsuite" OFF)
option(ENABLE_CRANELIFT_JIT "Enable Cranelift-based AOT compilation for WebAssembly" ${AETHERIS_OPTIONAL_COMPONENTS_DEFAULT})

set(LADYBIRD_CACHE_DIR "${PROJECT_BINARY_DIR}/../caches" CACHE PATH "Location of shared cache of downloaded files")
option(ENABLE_NETWORK_DOWNLOADS "Allow downloads of required files. If OFF, required files must already be present in LADYBIRD_CACHE_DIR" ON)

option(ENABLE_CLANG_PLUGINS "Enable building with the Clang plugins" OFF)
option(ENABLE_CLANG_PLUGINS_INVALID_FUNCTION_MEMBERS "Enable detecting invalid function types as members of GC-allocated objects" OFF)

if ((LINUX AND NOT ANDROID) OR BSD)
    set(freedesktop_files_default ON)
else()
    set(freedesktop_files_default OFF)
endif()

option(ENABLE_GUI_TARGETS "Enable building GUI targets" ON)
option(ENABLE_INSTALL_HEADERS "Enable installing headers" ${AETHERIS_ENABLE_INSTALL_HEADERS})
option(ENABLE_INSTALL_FREEDESKTOP_FILES "Enable installing .desktop and .service files" ${freedesktop_files_default})
option(LADYBIRD_ENABLE_CPPTRACE "Enable use of cpptrace as the default library for stacktraces. If not available falls back to backtrace.h" ON)
option(LADYBIRD_GENERATE_DSYM "Generate dSYM bundles for binaries and libraries (macOS only)" OFF)
option(ENABLE_CI_BASELINE_CPU "Use a baseline CPU target for improved ccache sharing" OFF)

# lto1 uses a crazy amount of RAM in static builds.
# Disable LTO for static gcc builds unless explicitly asked for.
if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND NOT BUILD_SHARED_LIBS)
    set(RELEASE_LTO_DEFAULT OFF)
else()
    set(RELEASE_LTO_DEFAULT ON)
endif()

option(ENABLE_ADDRESS_SANITIZER "Enable address sanitizer testing in gcc/clang" OFF)
option(ENABLE_MEMORY_SANITIZER "Enable memory sanitizer testing in gcc/clang" OFF)
option(ENABLE_FUZZERS "Build fuzzing targets" OFF)
option(ENABLE_FUZZERS_LIBFUZZER "Build fuzzers using Clang's libFuzzer" OFF)
option(ENABLE_FUZZERS_OSSFUZZ "Build OSS-Fuzz compatible fuzzers" OFF)
option(ENABLE_LAGOM_CCACHE "Enable ccache for Lagom builds" ON)
set(LAGOM_USE_LINKER "" CACHE STRING "The linker to use (e.g. lld, mold) instead of the system default")
set(LAGOM_LINK_POOL_SIZE "" CACHE STRING "The maximum number of parallel jobs to use for linking")
option(ENABLE_LTO_FOR_RELEASE "Enable link-time optimization for release builds" ${RELEASE_LTO_DEFAULT})
option(ENABLE_LAGOM_COVERAGE_COLLECTION "Enable code coverage instrumentation for lagom binaries in clang" OFF)

if (ENABLE_FUZZERS_LIBFUZZER)
    # With libfuzzer, we need to avoid a duplicate main() linker error giving false negatives
    set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY CACHE STRING "Type of target to use for try_compile()" FORCE)
endif()

include(${CMAKE_CURRENT_LIST_DIR}/lagom_install_options.cmake)
