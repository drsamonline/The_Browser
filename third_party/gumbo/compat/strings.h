/* <strings.h> for MSVC.
 *
 * gumbo is POSIX C: it includes <strings.h> for strcasecmp/strncasecmp, and MSVC
 * has neither the header nor those names — it spells them _stricmp/_strnicmp and
 * declares them in <string.h>. Without this, every gumbo object fails with
 *
 *     fatal error C1083: Cannot open include file: 'strings.h'
 *
 * This directory is on the include path only for MSVC (see src/CMakeLists.txt),
 * so every other platform still gets the real system header.
 */
#pragma once

#ifndef AETHERIS_COMPAT_STRINGS_H
#define AETHERIS_COMPAT_STRINGS_H

#include <string.h>

#define strcasecmp _stricmp
#define strncasecmp _strnicmp

#endif /* AETHERIS_COMPAT_STRINGS_H */
