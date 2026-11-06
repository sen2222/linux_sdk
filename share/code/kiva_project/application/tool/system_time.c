#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include <stdatomic.h>
#include <stdint.h>
#include <time.h>
#include <errno.h>
#include "curl/curl.h"
#include "cJSON.h"
#include "app_config.h"
#include "system_time.h"





struct __system_time_ctx
{
    atomic_uint time_sync_flag;
    int curl_initialized;
};

struct __time_response
{
    char data[2048];
    size_t size;
};

static struct __system_time_ctx system_time_ctx = {0};

/* curl可能分多次返回数据，逐次追加，并预留字符串结尾。 */
static size_t time_write_cb(char *data, size_t size, size_t count, void *user_data)
{
    struct __time_response *response = user_data;
    size_t bytes;
    if (size != 0 && count > SIZE_MAX / size)
        return 0;
    bytes = size * count;
    if (bytes > sizeof(response->data) - response->size - 1)
    {
        APP_LOG(LOG_ERRO, "Time response is too large\n");
        return 0;
    }
    memcpy(response->data + response->size, data, bytes);
    response->size += bytes;
    response->data[response->size] = '\0';
    return bytes;
}

int system_time_init(void)
{
    CURLcode result;
    if (system_time_ctx.curl_initialized)
        return 0;
    /* 在启动LVGL线程前设置应用时区；已有TZ配置时沿用原配置。 */
    if (getenv("TZ") == NULL && setenv("TZ", "CST-8", 0) < 0)
        return -1;
    tzset();
    result = curl_global_init(CURL_GLOBAL_DEFAULT);
    if (result != CURLE_OK)
    {
        APP_LOG(LOG_ERRO, "curl global init failed: %s\n", curl_easy_strerror(result));
        return -1;
    }
    if (atexit(curl_global_cleanup) != 0)
    {
        curl_global_cleanup();
        return -1;
    }
    system_time_ctx.curl_initialized = 1;
    return 0;
}

int system_time_sync(void)
{
    CURL *curl = NULL;
    CURLcode result;
    struct __time_response response = {{0}, 0};
    cJSON *root = NULL;
    cJSON *data;
    cJSON *timestamp;
    const char *parse_end;
    char *timestamp_end;
    unsigned long long timestamp_ms;
    struct timespec now;
    struct tm local_time;
    char text[32];
    char error_text[CURL_ERROR_SIZE] = {0};
    long http_code = 0;
    int ret = -1;

    if (!system_time_ctx.curl_initialized)
        return -1;
    curl = curl_easy_init();
    if (curl == NULL)
        return -1;
    /* HTTP时间接口不需要CA证书，网络请求不会阻塞LVGL刷新线程。 */
    if (curl_easy_setopt(curl, CURLOPT_URL, SYSTEM_TIME_URL) != CURLE_OK ||
        curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, error_text) != CURLE_OK ||
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L) != CURLE_OK ||
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L) != CURLE_OK ||
        curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L) != CURLE_OK ||
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, time_write_cb) != CURLE_OK ||
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response) != CURLE_OK)
    {
        APP_LOG(LOG_ERRO, "Failed to configure time request\n");
        goto SYSTEM_TIME_SYNC_ERRO;
    }
    result = curl_easy_perform(curl);
    if (result != CURLE_OK)
    {
        APP_LOG(LOG_ERRO, "Time request failed (%d): %s\n", (int)result,
                error_text[0] ? error_text : curl_easy_strerror(result));
        goto SYSTEM_TIME_SYNC_ERRO;
    }
    if (curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code) != CURLE_OK || http_code != 200)
    {
        APP_LOG(LOG_ERRO, "Time request HTTP status: %ld\n", http_code);
        goto SYSTEM_TIME_SYNC_ERRO;
    }
    root = cJSON_ParseWithLengthOpts(response.data, response.size + 1, &parse_end, 1);
    if (root == NULL || parse_end != response.data + response.size)
    {
        APP_LOG(LOG_ERRO, "Invalid time response JSON\n");
        goto SYSTEM_TIME_SYNC_ERRO;
    }
    data = cJSON_GetObjectItemCaseSensitive(root, "data");
    timestamp = cJSON_GetObjectItemCaseSensitive(data, "t");
    /* 淘宝返回data.t，例如"1793547755424"，单位为毫秒。 */
    if (!cJSON_IsObject(data) || !cJSON_IsString(timestamp) ||
        timestamp->valuestring[0] < '0' || timestamp->valuestring[0] > '9')
    {
        APP_LOG(LOG_ERRO, "Invalid network timestamp\n");
        goto SYSTEM_TIME_SYNC_ERRO;
    }
    errno = 0;
    timestamp_ms = strtoull(timestamp->valuestring, &timestamp_end, 10);
    if (errno == ERANGE || *timestamp_end != '\0' || timestamp_ms < 946684800000ULL ||
        (sizeof(time_t) <= 4 && timestamp_ms / 1000 > (unsigned long long)INT32_MAX))
    {
        APP_LOG(LOG_ERRO, "Invalid network timestamp\n");
        goto SYSTEM_TIME_SYNC_ERRO;
    }
    now.tv_sec = (time_t)(timestamp_ms / 1000);
    now.tv_nsec = (long)(timestamp_ms % 1000) * 1000000L;
    /* 时间戳以UTC为基准，不能再加8小时；时区只影响显示。 */
    if (clock_settime(CLOCK_REALTIME, &now) < 0)
    {
        APP_LOG(LOG_ERRO, "Set system time failed: %s\n", strerror(errno));
        goto SYSTEM_TIME_SYNC_ERRO;
    }
    atomic_store(&system_time_ctx.time_sync_flag, 1);
    if (localtime_r(&now.tv_sec, &local_time) != NULL &&
        strftime(text, sizeof(text), "%Y-%m-%d %H:%M:%S %z", &local_time) > 0)
        APP_LOG(LOG_INFO, "System time synchronized: %s\n", text);
    ret = 0;
SYSTEM_TIME_SYNC_ERRO:
    cJSON_Delete(root);
    curl_easy_cleanup(curl);
    return ret;
}

int system_time_get_sync_flag(void)
{
    return atomic_load(&system_time_ctx.time_sync_flag);
}









