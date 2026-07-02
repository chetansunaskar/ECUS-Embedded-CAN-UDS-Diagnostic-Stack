/**
 * @file    Main.h
 * @brief   Application entry-point declarations.
 *
 * @author  Chetan S Sunaskar (Embedded Engineer)
 * @version 1.0.0
 */

#ifndef MAIN_H
#define MAIN_H

#include "hal/Platform.h"
#include "services/Logger.h"

/** Command-line options parsed from argv. */
typedef struct AppOptions
{
    LogLevel    logLevel;       /**< -v / -q adjust this.            */
    const char *logFilePath;    /**< -l <path>  (NULL = stdout only) */
    bool        colorOutput;    /**< -c  enable ANSI colour logs.    */
    bool        batchMode;      /**< -b <script> run commands then exit. */
    const char *batchScript;    /**< Path to batch command script.   */
    bool        showVersion;    /**< --version                       */
    bool        showHelp;       /**< --help / -h                     */
} AppOptions;

#endif /* MAIN_H */
