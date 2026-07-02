/**
 * @file    Logger.c
 * @brief   Implementation of the ECUS variadic logging framework.
 *
 * C concepts demonstrated:
 *   va_list / va_start / va_end (variadic functions),
 *   POSIX pthread_mutex (thread safety),
 *   FILE* I/O, fprintf, vfprintf,
 *   static module state, const char* arrays,
 *   conditional compilation (#if / #ifdef),
 *   function pointers (internal dispatch),
 *   time()/localtime() for timestamps,
 *   safe string handling (snprintf, never strcpy).
 *
 * @author  Chetan S Sunaskar (Embedded Engineer)
 * 
 * @version 1.0.0
 */

#include "services/Logger.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>   /* POSIX threads for mutex */

/* =========================================================================
 * ANSI colour escape codes (conditional compilation)
 * ========================================================================= */
#ifdef ECUS_LOG_COLOR
#  define CLR_RESET   "\033[0m"
#  define CLR_TRACE   "\033[90m"   /* dark grey  */
#  define CLR_DEBUG   "\033[36m"   /* cyan       */
#  define CLR_INFO    "\033[32m"   /* green      */
#  define CLR_WARN    "\033[33m"   /* yellow     */
#  define CLR_ERROR   "\033[31m"   /* red        */
#else
#  define CLR_RESET   ""
#  define CLR_TRACE   ""
#  define CLR_DEBUG   ""
#  define CLR_INFO    ""
#  define CLR_WARN    ""
#  define CLR_ERROR   ""
#endif /* ECUS_LOG_COLOR */

/* =========================================================================
 * Module-private constants and state
 * ========================================================================= */

/** Maximum formatted log line length (stack-allocated buffer). */
#define LOG_BUF_SIZE  512U

/** String representations of log levels — pointer array (no heap). */
static const char * const k_levelStr[] =
{
    "TRACE",   /* LOG_LEVEL_TRACE */
    "DEBUG",   /* LOG_LEVEL_DEBUG */
    "INFO ",   /* LOG_LEVEL_INFO  */
    "WARN ",   /* LOG_LEVEL_WARN  */
    "ERROR",   /* LOG_LEVEL_ERROR */
};

/** ANSI colour strings indexed by LogLevel. */
static const char * const k_levelColor[] =
{
    CLR_TRACE,
    CLR_DEBUG,
    CLR_INFO,
    CLR_WARN,
    CLR_ERROR,
};

/** Module state (static = translation-unit scope, zero-initialised). */
static struct
{
    bool            initialised;
    LogLevel        minLevel;
    bool            echoToStdout;
    bool            colorEnabled;
    bool            timestampEnabled;
    FILE           *logFile;          /**< NULL = stdout only */
    pthread_mutex_t mutex;
} s_log;

/* =========================================================================
 * Private helpers
 * ========================================================================= */

/**
 * @brief  Format a timestamp prefix into @p buf (at most @p size bytes).
 *         Uses localtime() — non-reentrant, but called under the log mutex.
 */
static void FormatTimestamp(char *restrict buf, size_t size)
{
    time_t     now = time(NULL);
    const struct tm *tm_info;

    /* MISRA deviate: localtime is non-reentrant; protected by mutex here. */
    MISRA_DEVIATE(21.10, "Protected by logger mutex — single-threaded access")
    tm_info = localtime(&now);

    if (tm_info != NULL)
    {
        /* Safe: snprintf never overflows the destination buffer. */
        (void)snprintf(buf, size,
                       "%04d-%02d-%02d %02d:%02d:%02d",
                       tm_info->tm_year + 1900,
                       tm_info->tm_mon  + 1,
                       tm_info->tm_mday,
                       tm_info->tm_hour,
                       tm_info->tm_min,
                       tm_info->tm_sec);
    }
    else
    {
        (void)strncpy(buf, "0000-00-00 00:00:00", size - 1U);
        buf[size - 1U] = '\0';
    }
}

/**
 * @brief  Extract the base filename from a full path.
 *         Returns a pointer into the original string — no allocation.
 */
static const char *BaseName(const char *path)
{
    const char *slash;

    if (path == NULL)
    {
        return "<null>";
    }

    /* Search for last '/' (Linux) or '\\' (Windows). */
    slash = strrchr(path, '/');
    if (slash == NULL)
    {
        slash = strrchr(path, '\\');
    }

    return (slash != NULL) ? (slash + 1) : path;
}

/* =========================================================================
 * Public API implementation
 * ========================================================================= */

EcusStatus Logger_Init(const LoggerConfig *config)
{
    ECUS_CHECK(config != NULL, ECUS_ERR_NULL_PTR, return ECUS_ERR_NULL_PTR);

    if (s_log.initialised)
    {
        return ECUS_ERR_ALREADY_INIT;
    }

    /* Zero-initialise the state struct (defensive). */
    (void)memset(&s_log, 0, sizeof(s_log));

    s_log.minLevel         = config->minLevel;
    s_log.echoToStdout     = config->echoToStdout;
    s_log.colorEnabled     = config->colorEnabled;
    s_log.timestampEnabled = config->timestampEnabled;
    s_log.logFile          = NULL;

    /* Open log file if path provided. */
    if (config->logFilePath != NULL)
    {
        s_log.logFile = fopen(config->logFilePath, "a");
        if (s_log.logFile == NULL)
        {
            (void)fprintf(stderr,
                          "[Logger] Cannot open log file: %s\n",
                          config->logFilePath);
            return ECUS_ERR_LOG_INIT;
        }
    }

    /* Initialise the POSIX mutex (default attributes). */
    if (pthread_mutex_init(&s_log.mutex, NULL) != 0)
    {
        if (s_log.logFile != NULL)
        {
            (void)fclose(s_log.logFile);
            s_log.logFile = NULL;
        }
        return ECUS_ERR_LOG_INIT;
    }

    s_log.initialised = true;
    return ECUS_OK;
}

/* -------------------------------------------------------------------------- */

void Logger_Deinit(void)
{
    if (!s_log.initialised)
    {
        return;
    }

    (void)pthread_mutex_lock(&s_log.mutex);

    if (s_log.logFile != NULL)
    {
        (void)fflush(s_log.logFile);
        (void)fclose(s_log.logFile);
        s_log.logFile = NULL;
    }

    (void)pthread_mutex_unlock(&s_log.mutex);
    (void)pthread_mutex_destroy(&s_log.mutex);

    s_log.initialised = false;
}

/* -------------------------------------------------------------------------- */

void Logger_SetLevel(LogLevel level)
{
    if (s_log.initialised)
    {
        (void)pthread_mutex_lock(&s_log.mutex);
        s_log.minLevel = level;
        (void)pthread_mutex_unlock(&s_log.mutex);
    }
}

/* -------------------------------------------------------------------------- */

void Logger_Write(LogLevel    level,
                  const char *file,
                  uint32_t    line,
                  const char *func,
                  const char *fmt,
                  ...)
{
    char       msgBuf[LOG_BUF_SIZE];
    char       tsBuf[40];
    va_list    args;
    FILE      *targets[2];
    size_t     targetCount = 0U;
    const char *color      = "";
    const char *reset      = "";

    /* Early-out if logger not ready or level is filtered. */
    if (!s_log.initialised || (level < s_log.minLevel) ||
        ((uint32_t)level >= ECUS_ARRAY_SIZE(k_levelStr)))
    {
        return;
    }

    /* Acquire mutex before any output. */
    (void)pthread_mutex_lock(&s_log.mutex);

    /* Determine output targets. */
    if (s_log.logFile != NULL)
    {
        targets[targetCount] = s_log.logFile;
        targetCount++;
    }
    if ((s_log.logFile == NULL) || s_log.echoToStdout)
    {
        targets[targetCount] = stdout;
        targetCount++;
    }

    /* Colour codes (stdout only). */
    if (s_log.colorEnabled)
    {
        color = k_levelColor[(uint32_t)level];
        reset = CLR_RESET;
    }

    /* Format user message via variadic API. */
    va_start(args, fmt);
    (void)vsnprintf(msgBuf, sizeof(msgBuf), fmt, args);
    va_end(args);
    msgBuf[sizeof(msgBuf) - 1U] = '\0';  /* guarantee NUL termination */

    /* Timestamp. */
    if (s_log.timestampEnabled)
    {
        FormatTimestamp(tsBuf, sizeof(tsBuf));
    }
    else
    {
        tsBuf[0] = '\0';
    }

    /* Emit to each target. */
    for (size_t t = 0U; t < targetCount; t++)
    {
        bool useColor = (s_log.colorEnabled && (targets[t] == stdout));

        if (s_log.timestampEnabled)
        {
            (void)fprintf(targets[t], "%s%s[%s] %-20s:%4u | %s%s\n",
                          useColor ? color : "",
                          tsBuf[0] ? " " : "",
                          k_levelStr[(uint32_t)level],
                          BaseName(file),
                          line,
                          msgBuf,
                          useColor ? reset : "");
        }
        else
        {
            (void)fprintf(targets[t], "%s[%s] %-20s:%4u | %s%s\n",
                          useColor ? color : "",
                          k_levelStr[(uint32_t)level],
                          BaseName(file),
                          line,
                          msgBuf,
                          useColor ? reset : "");
        }
    }

    /* Flush immediately so logs are visible even on crash. */
    for (size_t t = 0U; t < targetCount; t++)
    {
        (void)fflush(targets[t]);
    }

    ECUS_UNUSED_PARAM(func);  /* func reserved for future JSON logging */

    (void)pthread_mutex_unlock(&s_log.mutex);
}

/* -------------------------------------------------------------------------- */

void Logger_HexDump(const char *label, const uint8_t *buf, size_t len)
{
    char     lineBuf[80];
    char    *ptr;
    size_t   col;
    size_t   i;

    if (!s_log.initialised || (LOG_LEVEL_DEBUG < s_log.minLevel))
    {
        return;
    }

    LOG_DEBUG("Hex dump: %s (%zu bytes)", (label != NULL) ? label : "", len);

    /*
     * Classic 16-bytes-per-line hex+ASCII dump.
     * Demonstrates pointer arithmetic: ptr advances through lineBuf.
     */
    for (i = 0U; i < len; i += 16U)
    {
        ptr = lineBuf;
        /* Address column. */
        ptr += snprintf(ptr,
                        (size_t)(lineBuf + sizeof(lineBuf) - ptr),
                        "  %04zx  ", i);

        /* Hex columns (up to 16 bytes). */
        for (col = 0U; col < 16U; col++)
        {
            if ((i + col) < len)
            {
                ptr += snprintf(ptr,
                                (size_t)(lineBuf + sizeof(lineBuf) - ptr),
                                "%02X ", buf[i + col]);
            }
            else
            {
                ptr += snprintf(ptr,
                                (size_t)(lineBuf + sizeof(lineBuf) - ptr),
                                "   ");
            }
            if (col == 7U)
            {
                ptr += snprintf(ptr,
                                (size_t)(lineBuf + sizeof(lineBuf) - ptr),
                                " ");
            }
        }

        /* ASCII column. */
        ptr += snprintf(ptr,
                        (size_t)(lineBuf + sizeof(lineBuf) - ptr),
                        " |");
        for (col = 0U; (col < 16U) && ((i + col) < len); col++)
        {
            uint8_t c = buf[i + col];
            ptr += snprintf(ptr,
                            (size_t)(lineBuf + sizeof(lineBuf) - ptr),
                            "%c", ((c >= 0x20U) && (c < 0x7FU)) ? (char)c : '.');
        }
        (void)snprintf(ptr,
                       (size_t)(lineBuf + sizeof(lineBuf) - ptr),
                       "|");

        LOG_DEBUG("%s", lineBuf);
    }
}
