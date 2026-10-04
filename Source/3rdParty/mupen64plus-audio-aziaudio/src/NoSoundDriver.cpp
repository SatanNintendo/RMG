/****************************************************************************
*                                                                           *
* Azimer's HLE Audio Plugin - Mupen64Plus Port                              *
* NoSound Driver implementation                                             *
*                                                                           *
* License: GNU/GPLv2 http://www.gnu.org/licenses/gpl-2.0.html               *
*                                                                           *
****************************************************************************/

#include "NoSoundDriver.h"
#include "SoundDriverRegistrar.h"


#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#include <pthread.h>
#endif

/* Register this driver with the factory */
REGISTER_DRIVER(NoSoundDriver, SND_DRIVER_NOSOUND, "No Sound Driver", 0)

bool NoSoundDriver::ValidateDriver()
{
    return true;
}

NoSoundDriver::NoSoundDriver()
{
    m_Running = false;
    m_LastConsumeTick = 0;
#ifdef _WIN32
    m_Thread = NULL;
#else
    m_ThreadStarted = false;
#endif
}

NoSoundDriver::~NoSoundDriver()
{
    DeInitialize();
}

/* Cross-platform millisecond sleep */
static void ns_sleep_ms(u32 ms)
{
#ifdef _WIN32
    Sleep(ms);
#else
    usleep(ms * 1000u);
#endif
}

/* Cross-platform tick counter in milliseconds */
static u32 ns_tick_ms(void)
{
#ifdef _WIN32
    return (u32)GetTickCount();
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (u32)(ts.tv_sec * 1000ull + ts.tv_nsec / 1000000ull);
#endif
}

#ifndef _WIN32
static void* NoSoundThreadProc(void *param)
{
    class NoSoundAccess : public NoSoundDriver
    {
    public:
        void Run() { ThreadProc(); }
    };
    NoSoundAccess *driver = (NoSoundAccess *)param;
    driver->Run();
    return NULL;
}
#endif

void NoSoundDriver::ThreadProc()
{
    m_LastConsumeTick = ns_tick_ms();

    while (m_Running)
    {
        ns_sleep_ms(5);

        u32 now = ns_tick_ms();
        u32 elapsed = now - m_LastConsumeTick;
        if (elapsed == 0)
            continue;
        if (elapsed > 500)
            elapsed = 500;              /* catch-up guard */

        m_LastConsumeTick = now;

        u32 freq = m_SamplesPerSecond;
        if (freq == 0)
            freq = 44100;

        /* Consume exactly the amount of audio that passed in real time
           so the A/V sync throttle behaves as with a real device. */
        u64 bytes = (u64)freq * 4ull * elapsed / 1000ull;
        ConsumeBuffered((u32)bytes);
    }
}

Boolean NoSoundDriver::Initialize()
{
    return TRUE;
}

void NoSoundDriver::DeInitialize()
{
    StopAudio();
}

void NoSoundDriver::StopAudio()
{
    if (!m_Running)
        return;

    m_Running = false;

#ifdef _WIN32
    if (m_Thread != NULL)
    {
        DWORD result = WaitForSingleObject(m_Thread, 2000);
        if (result != WAIT_OBJECT_0)
            TerminateThread(m_Thread, 0);
        CloseHandle(m_Thread);
        m_Thread = NULL;
    }
#else
    if (m_ThreadStarted)
    {
        pthread_join(m_Thread, NULL);
        m_ThreadStarted = false;
    }
#endif
}

void NoSoundDriver::StartAudio()
{
    if (m_Running)
        return;

    m_Running = true;

#ifdef _WIN32
    m_Thread = CreateThread(NULL, 0, [](LPVOID param) -> DWORD
    {
        NoSoundDriver *driver = (NoSoundDriver *)param;
        driver->ThreadProc();
        return 0;
    }, this, 0, NULL);
#else
    if (pthread_create(&m_Thread, NULL, NoSoundThreadProc, this) == 0)
        m_ThreadStarted = true;
#endif
}

void NoSoundDriver::SetFrequency(u32 Frequency)
{
    /* Nothing to do - ConsumeBuffered uses m_SamplesPerSecond which the
       base class keeps in sync in AI_SetFrequency(). */
}
