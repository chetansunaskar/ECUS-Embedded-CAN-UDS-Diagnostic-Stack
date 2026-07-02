/**
 * @file    SignalHandler.c
 * @brief   POSIX signal handler implementation.
 *
 * C concepts demonstrated:
 *   signal() / sigaction() POSIX API,
 *   volatile sig_atomic_t (the correct type for signal flags),
 *   function pointer callback for application-layer shutdown hook.
 *
 * @author  Chetan S Sunaskar (Embedded Engineer)
 * @version 1.0.0
 */

#include "services/SignalHandler.h"
#include "services/Logger.h"

#ifdef _WIN32
    #include <signal.h>
#else
    #include <signal.h>
    #include <string.h>
#endif

/* =========================================================================
 * Module state
 *
 * volatile sig_atomic_t is the ONLY type guaranteed safe to read/write
 * inside a signal handler (C11 §7.14.1.1).  Using int or bool here would
 * be undefined behaviour on some platforms.
 * ========================================================================= */
static volatile sig_atomic_t s_shutdownFlag = 0;
static ShutdownCallbackFn    s_shutdownCb   = NULL;

/* =========================================================================
 * Private: signal handler
 *
 * Must be async-signal-safe: only write to volatile sig_atomic_t,
 * call async-signal-safe functions (write(), _exit()), nothing else.
 * ========================================================================= */
static void OnSignal(int signo)
{
    /* Record which signal was received (not used further but good practice). */
    (void)signo;

    /* Set the shutdown flag — checked by the main loop. */
    s_shutdownFlag = 1;

    /*
     * We do NOT call the shutdown callback here — signal handlers must be
     * async-signal-safe and most C library functions are not.  Instead,
     * the main loop calls SignalHandler_ShutdownRequested() and invokes
     * the callback from a safe context.
     */
}

/* =========================================================================
 * Public API
 * ========================================================================= */

EcusStatus SignalHandler_Init(ShutdownCallbackFn cb)
{
    s_shutdownCb   = cb;
    s_shutdownFlag = 0;

#ifdef _WIN32

    if (signal(SIGINT, OnSignal) == SIG_ERR)
    {
        return ECUS_ERR_GENERIC;
    }

    /* SIGTERM support is implementation dependent on Windows.
       Register it only if available. */
#ifdef SIGTERM
    if (signal(SIGTERM, OnSignal) == SIG_ERR)
    {
        return ECUS_ERR_GENERIC;
    }
#endif

    LOG_DEBUG("Signal handlers registered (Windows)");

#else

    struct sigaction sa;

    (void)memset(&sa, 0, sizeof(sa));

    sa.sa_handler = OnSignal;
    sa.sa_flags   = 0U;

    (void)sigemptyset(&sa.sa_mask);

    if (sigaction(SIGINT, &sa, NULL) != 0)
    {
        return ECUS_ERR_GENERIC;
    }

    if (sigaction(SIGTERM, &sa, NULL) != 0)
    {
        return ECUS_ERR_GENERIC;
    }

    LOG_DEBUG("Signal handlers registered (POSIX)");

#endif

    return ECUS_OK;
}

/* -------------------------------------------------------------------------- */

bool SignalHandler_ShutdownRequested(void)
{
    if (s_shutdownFlag != 0)
    {
        if (s_shutdownCb != NULL)
        {
            ShutdownCallbackFn cb = s_shutdownCb;
            s_shutdownCb = NULL;
            cb();
        }

        return true;
    }

    return false;
}
