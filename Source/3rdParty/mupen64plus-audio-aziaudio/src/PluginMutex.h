/****************************************************************************
*                                                                           *
* Azimer's HLE Audio Plugin - Mupen64Plus Port                              *
* PluginMutex.h - lightweight synchronization primitives                    *
*                                                                           *
* WHY THIS EXISTS (MinGW minimal-size builds):                              *
*                                                                           *
* With the MinGW-w64 "posix threads" toolchains (the default for both the   *
* cross-compiler and MSYS2's MINGW64/UCRT64 environments), libstdc++'s      *
* std::mutex::lock() and std::call_once() reference the fatal-error helper  *
* std::__throw_system_error() on their failure paths.  That single symbol  *
* pulls libstdc++'s system_error.o -> sstream -> the ENTIRE locale /        *
* iostream machinery (num_get, money_get, time_get, codecvt, gdtoa, ...)   *
* into a statically linked plugin: several hundred kilobytes of code that   *
* can never execute (a mutex lock failure on a valid CRITICAL_SECTION is    *
* not a recoverable event in this plugin anyway).                           *
*                                                                           *
* On Windows the plugin therefore uses the native primitives directly:     *
*   - PluginMutex   wraps CRITICAL_SECTION (recursive, like the            *
*                   winpthreads-based std::mutex this project shipped      *
*                   with, and with the same lock/unlock interface)         *
*   - PluginOnceFlag wraps INIT_ONCE (one-time initialisation with         *
*                   blocking, like std::once_flag / std::call_once)        *
* Both satisfy the C++ BasicLockable requirements, so std::lock_guard      *
* keeps working unchanged.  On non-Windows platforms the types are simple  *
* aliases for the std:: equivalents (shared objects link libstdc++         *
* dynamically there and the size argument does not apply).                *
*                                                                           *
* License: GNU/GPLv2 http://www.gnu.org/licenses/gpl-2.0.html               *
*                                                                           *
****************************************************************************/
#ifndef _PLUGIN_MUTEX_H_
#define _PLUGIN_MUTEX_H_

#ifdef _WIN32

#ifndef _WINDOWS_
#include <windows.h>
#endif

/* ---------------------------------------------------------------------------
 * PluginMutex - CRITICAL_SECTION wrapper (BasicLockable)
 *
 * Recursive, like the winpthreads-based std::mutex previously used here
 * (PTHREAD_MUTEX_DEFAULT in winpthreads is recursive).  None of the code
 * in this project relies on non-recursive deadlock detection; the
 * emulation-core -> backend call graph takes each mutex at most once per
 * thread.  Constructible as a file-scope static (InitializeCriticalSection
 * is safe during DLL static initialisation - kernel32 is always mapped).
 * ------------------------------------------------------------------------ */
class PluginMutex
{
public:
    PluginMutex() { InitializeCriticalSection(&m_cs); }
    ~PluginMutex() { DeleteCriticalSection(&m_cs); }

    PluginMutex(const PluginMutex &) = delete;
    PluginMutex &operator=(const PluginMutex &) = delete;

    void lock() { EnterCriticalSection(&m_cs); }
    void unlock() { LeaveCriticalSection(&m_cs); }
    bool try_lock() { return TryEnterCriticalSection(&m_cs) != FALSE; }

private:
    CRITICAL_SECTION m_cs;
};

/* ---------------------------------------------------------------------------
 * PluginOnceFlag / PluginCallOnce - one-time initialisation (INIT_ONCE)
 *
 * Equivalent to std::once_flag + std::call_once: exactly one caller runs
 * the function, concurrent callers block until it completes.
 * INIT_ONCE_STATIC_INIT is all zeroes, so a file-scope PluginOnceFlag needs
 * no constructor and no static-initialisation-order considerations.
 * ------------------------------------------------------------------------ */
struct PluginOnceFlag
{
    INIT_ONCE once;
};
#define PLUGIN_ONCE_FLAG_INIT { INIT_ONCE_STATIC_INIT }
template <typename Fn>
inline void PluginCallOnce(PluginOnceFlag &flag, Fn fn)
{
    BOOL pending = FALSE;

    if (InitOnceBeginInitialize(&flag.once, 0, &pending, NULL) != FALSE)
    {
        if (pending)
        {
            fn();
            InitOnceComplete(&flag.once, 0, NULL);
        }
        /* else: another thread completed the initialisation while we
           waited inside InitOnceBeginInitialize - nothing left to do. */
    }
    /* InitOnceBeginInitialize only fails on an invalid INIT_ONCE state,
       which cannot occur for a zero-initialised flag.  Doing nothing in
       that impossible case matches "initialisation did not happen". */
}

#else /* !_WIN32 */

#include <mutex>

/* POSIX builds: plain std::mutex (linked dynamically, no size concern). */
using PluginMutex    = std::mutex;
using PluginOnceFlag = std::once_flag;
#define PLUGIN_ONCE_FLAG_INIT {}

template <typename Fn>
inline void PluginCallOnce(PluginOnceFlag &flag, Fn fn)
{
    std::call_once(flag, fn);
}

#endif /* _WIN32 */

/* Common, platform-independent lock guard for PluginMutex. */
#include <mutex>
using PluginLockGuard = std::lock_guard<PluginMutex>;

#endif /* _PLUGIN_MUTEX_H_ */
