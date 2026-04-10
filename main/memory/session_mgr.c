#include "session_mgr.h"
#include "mimi_config.h"

#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <dirent.h>
#include <time.h>
#include <inttypes.h>
#include "esp_log.h"
#include "cJSON.h"

static const char *TAG = "session";

/* Compact threshold: when line count exceeds this, rewrite keeping only recent entries */
#define SESSION_COMPACT_THRESHOLD  (MIMI_SESSION_MAX_MSGS * 3)

/* Write counter: avoid re-scanning file on every append */
static int s_append_count = 0;
#define SESSION_COMPACT_CHECK_INTERVAL  SESSION_COMPACT_THRESHOLD

static void session_path(const char *channel, const char *chat_id, char *buf, size_t size)
{
    /* Use short prefix to keep filename within SPIFFS limits (32-64 chars) */
    const char *prefix = "s";
    if (channel) {
        if (strcmp(channel, "telegram") == 0) {
            prefix = "st";
        } else if (strcmp(channel, "feishu") == 0) {
            prefix = "sf";
        } else if (strcmp(channel, "cli") == 0) {
            prefix = "sc";
        } else if (strcmp(channel, "websocket") == 0) {
            prefix = "sw";
        }
    }
    
    /* Use FNV-1a hash of chat_id if too long to fit SPIFFS filename limit */
    size_t chat_id_len = strlen(chat_id);
    if (chat_id_len > 32) {
        /* FNV-1a 32-bit hash for collision-resistant short ID */
        uint32_t h = 2166136261u;
        for (size_t i = 0; i < chat_id_len; i++) {
            h ^= (unsigned char)chat_id[i];
            h *= 16777619u;
        }
        char short_id[12];
        snprintf(short_id, sizeof(short_id), "%08" PRIx32, h);
        snprintf(buf, size, "%s/%s%s.jl", MIMI_SPIFFS_BASE, prefix, short_id);
    } else {
        snprintf(buf, size, "%s/%s%s.jl", MIMI_SPIFFS_BASE, prefix, chat_id);
    }
}

esp_err_t session_mgr_init(void)
{
    ESP_LOGI(TAG, "Session manager initialized (files in %s)", MIMI_SPIFFS_BASE);
    return ESP_OK;
}

/**
 * Compact a session file: keep only the most recent MIMI_SESSION_MAX_MSGS lines.
 * Uses PSRAM for temporary storage to avoid excessive internal RAM usage.
 */
static void session_compact(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) return;

    /* Ring buffer of line pointers */
    char *lines[MIMI_SESSION_MAX_MSGS];
    int count = 0;
    int write_idx = 0;

    for (int i = 0; i < MIMI_SESSION_MAX_MSGS; i++) {
        lines[i] = NULL;
    }

    char line[2048];
    while (fgets(line, sizeof(line), f)) {
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n') line[len - 1] = '\0';
        if (line[0] == '\0') continue;

        if (count >= MIMI_SESSION_MAX_MSGS) {
            free(lines[write_idx]);
        }
        lines[write_idx] = strdup(line);
        write_idx = (write_idx + 1) % MIMI_SESSION_MAX_MSGS;
        if (count < MIMI_SESSION_MAX_MSGS) count++;
    }
    fclose(f);

    /* Write to temp file first — avoids data loss if interrupted */
    char tmp_path[100];
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", path);

    f = fopen(tmp_path, "w");
    if (!f) {
        for (int i = 0; i < MIMI_SESSION_MAX_MSGS; i++) free(lines[i]);
        ESP_LOGE(TAG, "Cannot create temp file for session compact");
        return;
    }

    int start = (count < MIMI_SESSION_MAX_MSGS) ? 0 : write_idx;
    bool write_ok = true;
    for (int i = 0; i < count; i++) {
        int idx = (start + i) % MIMI_SESSION_MAX_MSGS;
        if (lines[idx]) {
            if (fprintf(f, "%s\n", lines[idx]) < 0) {
                write_ok = false;
                break;
            }
        }
    }
    fclose(f);

    /* Cleanup line buffers */
    for (int i = 0; i < MIMI_SESSION_MAX_MSGS; i++) {
        free(lines[i]);
    }

    if (!write_ok) {
        ESP_LOGE(TAG, "Write error during session compact, keeping original");
        remove(tmp_path);
        return;
    }

    /* Atomic swap: remove original, rename temp */
    remove(path);
    if (rename(tmp_path, path) != 0) {
        ESP_LOGE(TAG, "Failed to rename temp session file");
        remove(tmp_path);
        return;
    }

    ESP_LOGI(TAG, "Session compacted: kept %d most recent entries", count);
}

esp_err_t session_append(const char *channel, const char *chat_id, const char *role, const char *content)
{
    char path[96];
    session_path(channel, chat_id, path, sizeof(path));

    FILE *f = fopen(path, "a");
    if (!f) {
        ESP_LOGE(TAG, "Cannot open session file %s", path);
        return ESP_FAIL;
    }

    cJSON *obj = cJSON_CreateObject();
    cJSON_AddStringToObject(obj, "role", role);
    cJSON_AddStringToObject(obj, "content", content);
    cJSON_AddNumberToObject(obj, "ts", (double)time(NULL));

    char *line = cJSON_PrintUnformatted(obj);
    cJSON_Delete(obj);

    if (line) {
        if (fprintf(f, "%s\n", line) < 0) {
            ESP_LOGE(TAG, "Failed to write to session file %s", path);
        }
        free(line);
    }

    fclose(f);

    /* Auto-compact: check only every N appends to reduce SPIFFS I/O */
    s_append_count++;
    if (s_append_count >= SESSION_COMPACT_CHECK_INTERVAL) {
        s_append_count = 0;
        f = fopen(path, "r");
        if (f) {
            int line_count = 0;
            char tmp[64];
            while (fgets(tmp, sizeof(tmp), f)) {
                /* Count actual newlines (a long line may span multiple fgets calls) */
                if (tmp[strlen(tmp) - 1] == '\n') {
                    line_count++;
                }
            }
            fclose(f);

            if (line_count > SESSION_COMPACT_THRESHOLD) {
                ESP_LOGI(TAG, "Session %s has %d lines (threshold %d), compacting...",
                         path, line_count, SESSION_COMPACT_THRESHOLD);
                session_compact(path);
            }
        }
    }

    return ESP_OK;
}

esp_err_t session_get_history_json(const char *channel, const char *chat_id, char *buf, size_t size, int max_msgs)
{
    char path[96];
    session_path(channel, chat_id, path, sizeof(path));

    FILE *f = fopen(path, "r");
    if (!f) {
        /* No history yet */
        snprintf(buf, size, "[]");
        return ESP_OK;
    }

    /* Read all lines into a ring buffer of cJSON objects */
    cJSON *messages[MIMI_SESSION_MAX_MSGS];
    int count = 0;
    int write_idx = 0;

    char line[2048];
    while (fgets(line, sizeof(line), f)) {
        /* Strip newline */
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n') line[len - 1] = '\0';
        if (line[0] == '\0') continue;

        cJSON *obj = cJSON_Parse(line);
        if (!obj) continue;

        /* Ring buffer: overwrite oldest if full */
        if (count >= max_msgs) {
            cJSON_Delete(messages[write_idx]);
        }
        messages[write_idx] = obj;
        write_idx = (write_idx + 1) % max_msgs;
        if (count < max_msgs) count++;
    }
    fclose(f);

    /* Build JSON array with only role + content */
    cJSON *arr = cJSON_CreateArray();
    int start = (count < max_msgs) ? 0 : write_idx;
    for (int i = 0; i < count; i++) {
        int idx = (start + i) % max_msgs;
        cJSON *src = messages[idx];

        cJSON *entry = cJSON_CreateObject();
        cJSON *role = cJSON_GetObjectItem(src, "role");
        cJSON *content = cJSON_GetObjectItem(src, "content");
        if (role && cJSON_IsString(role) && content && cJSON_IsString(content)) {
            cJSON_AddStringToObject(entry, "role", role->valuestring);
            cJSON_AddStringToObject(entry, "content", content->valuestring);
        }
        cJSON_AddItemToArray(arr, entry);
    }

    /* Cleanup ring buffer */
    int cleanup_start = (count < max_msgs) ? 0 : write_idx;
    for (int i = 0; i < count; i++) {
        int idx = (cleanup_start + i) % max_msgs;
        cJSON_Delete(messages[idx]);
    }

    char *json_str = cJSON_PrintUnformatted(arr);
    cJSON_Delete(arr);

    if (json_str) {
        strncpy(buf, json_str, size - 1);
        buf[size - 1] = '\0';
        free(json_str);
    } else {
        snprintf(buf, size, "[]");
    }

    return ESP_OK;
}

esp_err_t session_clear(const char *channel, const char *chat_id)
{
    char path[96];
    session_path(channel, chat_id, path, sizeof(path));

    if (remove(path) == 0) {
        ESP_LOGI(TAG, "Session %s cleared", chat_id);
        return ESP_OK;
    }
    return ESP_ERR_NOT_FOUND;
}

void session_list(void)
{
    DIR *dir = opendir(MIMI_SPIFFS_BASE);
    if (!dir) {
        ESP_LOGW(TAG, "Cannot open SPIFFS directory");
        return;
    }

    struct dirent *entry;
    int count = 0;
    while ((entry = readdir(dir)) != NULL) {
        /* Match all session files: st*, sf*, sc*, sw*, s* with .jl extension */
        if (strstr(entry->d_name, ".jl") && 
            (strncmp(entry->d_name, "st", 2) == 0 ||
             strncmp(entry->d_name, "sf", 2) == 0 ||
             strncmp(entry->d_name, "sc", 2) == 0 ||
             strncmp(entry->d_name, "sw", 2) == 0 ||
             strncmp(entry->d_name, "s", 1) == 0)) {
            ESP_LOGI(TAG, "  Session: %s", entry->d_name);
            count++;
        }
    }
    closedir(dir);

    if (count == 0) {
        ESP_LOGI(TAG, "  No sessions found");
    }
}
