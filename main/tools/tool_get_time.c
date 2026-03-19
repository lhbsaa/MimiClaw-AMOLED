#include "tool_get_time.h"
#include "mimi_config.h"
#include "proxy/http_proxy.h"

#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <sys/time.h>
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"

static const char *TAG = "tool_time";

static const char *MONTHS[] = {
    "Jan","Feb","Mar","Apr","May","Jun",
    "Jul","Aug","Sep","Oct","Nov","Dec"
};

/* Parse "Sat, 01 Feb 2025 10:25:00 GMT" → set system clock, return formatted string */
static bool parse_and_set_time(const char *date_str, char *out, size_t out_size)
{
    int day, year, hour, min, sec;
    char mon_str[4] = {0};

    if (sscanf(date_str, "%*[^,], %d %3s %d %d:%d:%d",
               &day, mon_str, &year, &hour, &min, &sec) != 6) {
        return false;
    }

    int mon = -1;
    for (int i = 0; i < 12; i++) {
        if (strcmp(mon_str, MONTHS[i]) == 0) { mon = i; break; }
    }
    if (mon < 0) return false;

    struct tm tm = {
        .tm_sec = sec, .tm_min = min, .tm_hour = hour,
        .tm_mday = day, .tm_mon = mon, .tm_year = year - 1900,
    };

    /* Convert UTC to epoch — mktime expects local, so temporarily set UTC */
    setenv("TZ", "UTC0", 1);
    tzset();
    time_t t = mktime(&tm);

    /* Restore timezone */
    setenv("TZ", MIMI_TIMEZONE, 1);
    tzset();

    if (t < 0) return false;

    struct timeval tv = { .tv_sec = t };
    settimeofday(&tv, NULL);

    /* Format in local time */
    struct tm local;
    localtime_r(&t, &local);
    strftime(out, out_size, "%Y-%m-%d %H:%M:%S %Z (%A)", &local);

    return true;
}

/* Fetch time via proxy: HEAD request to api.telegram.org, parse Date header */
static esp_err_t fetch_time_via_proxy(char *out, size_t out_size)
{
    ESP_LOGI(TAG, "Fetching time via proxy");
    proxy_conn_t *conn = proxy_conn_open("api.telegram.org", 443, 10000);
    if (!conn) {
        ESP_LOGE(TAG, "Failed to open proxy connection");
        return ESP_ERR_HTTP_CONNECT;
    }

    const char *req =
        "HEAD / HTTP/1.1\r\n"
        "Host: api.telegram.org\r\n"
        "Connection: close\r\n\r\n";

    if (proxy_conn_write(conn, req, strlen(req)) < 0) {
        ESP_LOGE(TAG, "Failed to write to proxy connection");
        proxy_conn_close(conn);
        return ESP_ERR_HTTP_WRITE_DATA;
    }

    char buf[1024];
    int total = 0;
    while (total < (int)sizeof(buf) - 1) {
        int n = proxy_conn_read(conn, buf + total, sizeof(buf) - 1 - total, 10000);
        if (n <= 0) break;
        total += n;
        buf[total] = '\0';
        if (strstr(buf, "\r\n\r\n")) break;
    }
    proxy_conn_close(conn);

    /* Find Date header */
    char *date_hdr = strcasestr(buf, "\r\nDate: ");
    if (!date_hdr) {
        ESP_LOGE(TAG, "No Date header found in response");
        return ESP_ERR_NOT_FOUND;
    }
    date_hdr += 8;

    char *eol = strstr(date_hdr, "\r\n");
    if (!eol) {
        ESP_LOGE(TAG, "No end of line found in Date header");
        return ESP_ERR_NOT_FOUND;
    }

    char date_val[64];
    size_t dlen = eol - date_hdr;
    if (dlen >= sizeof(date_val)) {
        ESP_LOGE(TAG, "Date header too long");
        return ESP_ERR_NOT_FOUND;
    }
    memcpy(date_val, date_hdr, dlen);
    date_val[dlen] = '\0';

    ESP_LOGI(TAG, "Parsed date from proxy: %s", date_val);
    if (!parse_and_set_time(date_val, out, out_size)) {
        ESP_LOGE(TAG, "Failed to parse time: %s", date_val);
        return ESP_FAIL;
    }
    return ESP_OK;
}

/* Event handler that captures the Date response header */
typedef struct {
    char date_val[64];
} time_header_ctx_t;

/**
 * HTTP event handler that captures the "Date" response header.
 *
 * esp_http_client_get_header() only accesses request headers, so response
 * headers must be captured here during HTTP_EVENT_ON_HEADER events.
 * The Date value is copied into the time_header_ctx_t provided via user_data.
 */
static esp_err_t time_http_event_handler(esp_http_client_event_t *evt)
{
    time_header_ctx_t *ctx = evt->user_data;
    if (evt->event_id == HTTP_EVENT_ON_HEADER) {
        if (strcasecmp(evt->header_key, "Date") == 0 && ctx) {
            strncpy(ctx->date_val, evt->header_value, sizeof(ctx->date_val) - 1);
            ctx->date_val[sizeof(ctx->date_val) - 1] = '\0';
        }
    }
    return ESP_OK;
}

/* Fetch time via direct HTTPS - try multiple servers */
static esp_err_t fetch_time_direct(char *out, size_t out_size)
{
    time_header_ctx_t ctx = {0};

    const char *servers[] = {
        "https://www.microsoft.com/",
        "https://www.apple.com/",
        "https://www.cloudflare.com/",
        NULL
    };

    for (int i = 0; servers[i] != NULL; i++) {
        ESP_LOGI(TAG, "Attempting to fetch time from %s", servers[i]);
        memset(&ctx, 0, sizeof(ctx));

        esp_http_client_config_t config = {
            .url = servers[i],
            .method = HTTP_METHOD_HEAD,
            .timeout_ms = 3000,
            .crt_bundle_attach = esp_crt_bundle_attach,
            .event_handler = time_http_event_handler,
            .user_data = &ctx,
        };

        esp_http_client_handle_t client = esp_http_client_init(&config);
        if (!client) {
            ESP_LOGE(TAG, "Failed to initialize HTTP client for %s", servers[i]);
            continue;
        }

        esp_err_t err = esp_http_client_perform(client);
        ESP_LOGI(TAG, "HTTP client perform result for %s: %s", servers[i], esp_err_to_name(err));
        
        esp_http_client_cleanup(client);

        if (err == ESP_OK && ctx.date_val[0] != '\0') {
            ESP_LOGI(TAG, "Received date header from %s: %s", servers[i], ctx.date_val);
            if (parse_and_set_time(ctx.date_val, out, out_size)) {
                ESP_LOGI(TAG, "Time fetched from %s", servers[i]);
                return ESP_OK;
            } else {
                ESP_LOGE(TAG, "Failed to parse time from %s: %s", servers[i], ctx.date_val);
            }
        } else if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to fetch time from %s: %s", servers[i], esp_err_to_name(err));
        } else {
            ESP_LOGE(TAG, "No date header received from %s", servers[i]);
        }
    }

    return ESP_ERR_HTTP_CONNECT;
}

esp_err_t tool_get_time_execute(const char *input_json, char *output, size_t output_size)
{
    ESP_LOGI(TAG, "Fetching current time...");

    esp_err_t err;
    if (http_proxy_is_enabled()) {
        ESP_LOGI(TAG, "Using proxy to fetch time");
        err = fetch_time_via_proxy(output, output_size);
    } else {
        ESP_LOGI(TAG, "Using direct connection to fetch time");
        err = fetch_time_direct(output, output_size);
    }

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Time fetched successfully: %s", output);
    } else {
        snprintf(output, output_size, "Error: failed to fetch time (%s)", esp_err_to_name(err));
        ESP_LOGE(TAG, "%s", output);
    }

    return err;
}
