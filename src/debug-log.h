#ifndef DEBUG_LOG_H
#define DEBUG_LOG_H

#include <stdio.h>

#ifndef LE_CONTROLLEUR_DEBUG_LOG
#define LE_CONTROLLEUR_DEBUG_LOG 1
#endif

#ifndef LE_CONTROLLEUR_DEBUG_LEVEL
#define LE_CONTROLLEUR_DEBUG_LEVEL 2
#endif

#ifndef LE_CONTROLLEUR_VERSION
#define LE_CONTROLLEUR_VERSION "0.1.0-dev"
#endif

#ifndef LE_CONTROLLEUR_GIT_HASH
#define LE_CONTROLLEUR_GIT_HASH "unknown"
#endif

#if LE_CONTROLLEUR_DEBUG_LOG
#define LOG_ERROR(tag, fmt, ...) printf("[%s][ERROR] " fmt "\n", tag, ##__VA_ARGS__)
#else
#define LOG_ERROR(tag, fmt, ...) ((void)0)
#endif

#if LE_CONTROLLEUR_DEBUG_LOG && (LE_CONTROLLEUR_DEBUG_LEVEL >= 1)
#define LOG_INFO(tag, fmt, ...) printf("[%s][INFO] " fmt "\n", tag, ##__VA_ARGS__)
#else
#define LOG_INFO(tag, fmt, ...) ((void)0)
#endif

#if LE_CONTROLLEUR_DEBUG_LOG && (LE_CONTROLLEUR_DEBUG_LEVEL >= 2)
#define LOG_TRACE(tag, fmt, ...) printf("[%s][TRACE] " fmt "\n", tag, ##__VA_ARGS__)
#else
#define LOG_TRACE(tag, fmt, ...) ((void)0)
#endif

#endif
