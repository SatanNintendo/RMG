/****************************************************************************
*                                                                           *
* Azimer's HLE Audio Plugin - Mupen64Plus Port                              *
* MinimalAlloc.cpp - malloc-backed replaceable allocation functions        *
* (MinGW minimal-size builds only)                                          *
*                                                                           *
* WHY THIS EXISTS:                                                          *
*                                                                           *
* On the MinGW-w64 "posix threads" toolchains, libstdc++'s operator new    *
* is implemented with an internal try/catch (even the std::nothrow form   *
* catches bad_alloc from the throwing form it calls).  Linking ANY of      *
* those operators into a statically linked plugin therefore drags in the   *
* whole C++ exception runtime: __cxa_* / SEH personality, the             *
* thread_local exception globals of libstdc++, GCC's emulated TLS         *
* (emutls), and with them a dynamic dependency on libwinpthread-1.dll     *
* (pthread_once / pthread_key_create) - hundreds of kilobytes of dead      *
* code plus a DLL the plugin would otherwise not need at all.              *
*                                                                           *
* The plugin allocates small, fixed, well-bounded objects only, every     *
* allocation already goes through std::nothrow variants with NULL checks   *
* (or through the factory fallbacks), and nothing catches.  For that      *
* shape of program the C++ standard explicitly allows a program to        *
* replace the replaceable global allocation functions                     *
* ([replacement.functions]); these minimal malloc-backed versions keep     *
* the exact std::nothrow semantics (return NULL on failure) while        *
* severing the exception-runtime dependency.  The matching operator       *
* delete / operator delete[] remain the libstdc++ ones - on MinGW both   *
* are plain free() wrappers over the same msvcrt heap malloc uses, so     *
* the allocator pairing stays valid.                                       *
*                                                                           *
* Guarded to MinGW builds only: MSVC links its own always-present CRT     *
* (nothing to save there), and on ELF platforms redefining these in a     *
* shared object could interpose the front-end's own operators.            *
*                                                                           *
* License: GNU/GPLv2 http://www.gnu.org/licenses/gpl-2.0.html               *
*                                                                           *
****************************************************************************/

#include "common.h"

#if defined(_WIN32) && defined(__GNUC__) && !defined(__clang__)

#include <new>
#include <stdlib.h>

/* malloc(0) may legally return NULL - pad to keep the size-0 case
 * working (an empty allocation must still yield a non-NULL, deletable
 * pointer for the nothrow forms). */
static void *azi_malloc(std::size_t size)
{
    if (size == 0)
        size = 1;
    return malloc(size);
}

/* Throwing forms: with exceptions disabled across the plugin a failed
 * allocation cannot propagate - abort is the only honest outcome (an
 * unhandled bad_alloc would terminate the process identically). */
void *operator new(std::size_t size)
{
    void *ptr = azi_malloc(size);
    if (ptr == NULL)
        abort();
    return ptr;
}

void *operator new[](std::size_t size)
{
    void *ptr = azi_malloc(size);
    if (ptr == NULL)
        abort();
    return ptr;
}

/* Nothrow forms: exactly the documented semantics (NULL on failure);
 * a null result makes the compiler skip the constructor call. */
void *operator new(std::size_t size, const std::nothrow_t &) noexcept
{
    return azi_malloc(size);
}

void *operator new[](std::size_t size, const std::nothrow_t &) noexcept
{
    return azi_malloc(size);
}

#endif /* _WIN32 && __GNUC__ && !__clang__ */
