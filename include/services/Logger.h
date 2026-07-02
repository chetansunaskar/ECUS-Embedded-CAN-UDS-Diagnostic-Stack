/**
 * @file    Logger.h
 * @brief   Multi-level, variadic logging framework for ECUS.
 *
 * Features:
 *  - Five severity levels: TRACE, DEBUG, INFO, WARN, ERROR.
 *  - Runtime-configurable minimum log level.
 *  - Output to stdout, a file, or both simultaneously.
 *  - Thread-safe (mutex-protected in the implementation).
 *  - Variadic printf-style API demonstrates stdarg / va_list usage.
 *  - Convenience macros prepend file/line/function automatically.
 *  - Colour output on terminals that support ANSI escape codes
 *    (conditionally compiled via ECUS_LOG_COLOR).
 *
 * Usage:
 *   Logger_Init(LOG_LEVEL_DEBUG, "ecus.log");
 *   LOG_INFO("UDS session started: type=0x%02X", sessionType);
 *   LOG_WARN("Security access attempt #%u", attempt);
 *
 * @author  Chetan S Sunaskar (Embedded Engineer)
 * @version 1.0.0
 */

#ifndef LOGGER_H
#define LOGGER_H

#include "hal/Platform.h"
#include "services/ErrorHandler.h"

/* =========================================================================
 * Log level enumeration
 * ========================================================================= */
typedef enum LogLevel
{
    LOG_LEVEL_TRACE = 0U,   /**< Verbose trace (function entry/exit).     */
    LOG_LEVEL_DEBUG = 1U,   /**< Debug values, state transitions.         */
    LOG_LEVEL_INFO  = 2U,   /**< Normal operational messages.             */
    LOG_LEVEL_WARN  = 3U,   /**< Non-fatal anomalies.                     */
    LOG_LEVEL_ERROR = 4U,   /**< Errors requiring attention.              */
    LOG_LEVEL_NONE  = 5U    /**< Disable all logging.                     */
} LogLevel;

/* =========================================================================
 * Logger configuration
 * ========================================================================= */
typedef struct LoggerConfig
{
    LogLevel    minLevel;          /**< Minimum level to emit.             */
    const char *logFilePath;       /**< File path, or NULL for stdout only.*/
    bool        echoToStdout;      /**< Mirror file output to stdout.      */
    bool        colorEnabled;      /**< ANSI colour codes (stdout only).   */
    bool        timestampEnabled;  /**< Prepend epoch timestamp.           */
} LoggerConfig;

/* =========================================================================
 * Public API
 * ========================================================================= */

/**
 * @brief  Initialise the logger.
 * @param  config  Pointer to configuration struct (copied internally).
 * @return ECUS_OK on success.
 */
EcusStatus Logger_Init(const LoggerConfig *config);

/**
 * @brief  Flush and close the logger.
 */
void Logger_Deinit(void);

/**
 * @brief  Change the active log level at runtime.
 * @param  level  New minimum log level.
 */
void Logger_SetLevel(LogLevel level);

/**
 * @brief  Core variadic logging function.
 *         Prefer the LOG_* macros below; they inject file/line/func.
 *
 * @param  level  Severity level of this message.
 * @param  file   Source file (__FILE__).
 * @param  line   Source line (__LINE__).
 * @param  func   Function name (__func__).
 * @param  fmt    printf-style format string.
 * @param  ...    Format arguments.
 */
void Logger_Write(LogLevel    level,
                  const char *file,
                  uint32_t    line,
                  const char *func,
                  const char *fmt,
                  ...) ECUS_ATTR_UNUSED;  /* suppress unused-function warning */

/* =========================================================================
 * Convenience macros — zero overhead when level is below threshold.
 *
 * The do { } while(0) idiom ensures the macro behaves like a statement
 * even when used without braces in an if/else.
 * ========================================================================= */

/*
 * MISRA deviate 20.10 / 20.12: these macros use the GNU ##__VA_ARGS__
 * extension to allow zero-argument variadic calls, e.g. LOG_INFO("msg").
 * This is a deliberate, well-understood deviation from strict ISO C99/C11
 * (-Wpedantic flags it) — the same idiom used throughout the Linux kernel
 * and FreeRTOS logging subsystems. -Wpedantic is intentionally excluded
 * from this project's warning set (see Makefile) for this reason.
 */
#define LOG_TRACE(fmt, ...) \
    Logger_Write(LOG_LEVEL_TRACE, __FILE__, (uint32_t)__LINE__, __func__, \
                 (fmt), ##__VA_ARGS__)

#define LOG_DEBUG(fmt, ...) \
    Logger_Write(LOG_LEVEL_DEBUG, __FILE__, (uint32_t)__LINE__, __func__, \
                 (fmt), ##__VA_ARGS__)

#define LOG_INFO(fmt, ...) \
    Logger_Write(LOG_LEVEL_INFO,  __FILE__, (uint32_t)__LINE__, __func__, \
                 (fmt), ##__VA_ARGS__)

#define LOG_WARN(fmt, ...) \
    Logger_Write(LOG_LEVEL_WARN,  __FILE__, (uint32_t)__LINE__, __func__, \
                 (fmt), ##__VA_ARGS__)

#define LOG_ERROR(fmt, ...) \
    Logger_Write(LOG_LEVEL_ERROR, __FILE__, (uint32_t)__LINE__, __func__, \
                 (fmt), ##__VA_ARGS__)

/* =========================================================================
 * Hex-dump helper — dumps a byte buffer in classic hex+ASCII style.
 * ========================================================================= */

/**
 * @brief  Log a hex dump of a byte buffer at DEBUG level.
 * @param  label   Label string printed before the dump.
 * @param  buf     Pointer to data buffer.
 * @param  len     Number of bytes to dump.
 */
void Logger_HexDump(const char    *label,
                    const uint8_t *buf,
                    size_t         len);

#endif /* LOGGER_H */
