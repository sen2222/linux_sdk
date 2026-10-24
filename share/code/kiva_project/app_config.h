#ifndef __APP_CONFIG_H__
#define __APP_CONFIG_H__


#include <stdio.h>


#define LOG_ERRO    0
#define LOG_WARN    1
#define LOG_INFO    2
#define LOG_DBUG    3

#define APP_LOG_LEVEL    LOG_DBUG

#define APP_LOG(level, fmt, ...)                                                                                                                                                                                                \
do                                                                                                                                                                                                                              \
{                                                                                                                                                                                                                               \
    if ((level) <= APP_LOG_LEVEL)                                                                                                                                                                                               \
    {                                                                                                                                                                                                                           \
        switch (level)                                                                                                                                                                                                          \
        {                                                                                                                                                                                                                       \
        case LOG_ERRO:                                                                                                                                                                                                          \
            printf("[APP-ERRO] %s:%d: " fmt "", __func__, __LINE__, ##__VA_ARGS__); break;                                                                                                                                      \
        case LOG_WARN:                                                                                                                                                                                                          \
            printf("[APP-WARN] %s:%d: " fmt "", __func__, __LINE__, ##__VA_ARGS__); break;                                                                                                                                      \
        case LOG_INFO:                                                                                                                                                                                                          \
            printf("[APP-INFO] %s:%d: " fmt "", __func__, __LINE__, ##__VA_ARGS__); break;                                                                                                                                      \
        case LOG_DBUG:                                                                                                                                                                                                          \
            printf("[APP-DBUG] %s:%d: " fmt "", __func__, __LINE__, ##__VA_ARGS__); break;                                                                                                                                      \
        }                                                                                                                                                                                                                       \
    }                                                                                                                                                                                                                           \
} while (0)





#endif
