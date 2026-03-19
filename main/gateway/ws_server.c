#include "ws_server.h"
#include "mimi_config.h"
#include "bus/message_bus.h"

#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_http_server.h"
#include "cJSON.h"
#include "freertos/semphr.h"

static const char *TAG = "ws";

static httpd_handle_t s_server = NULL;
static SemaphoreHandle_t s_clients_mutex = NULL;  // 客户端列表互斥锁

/* Simple client tracking */
typedef struct {
    int fd;
    char chat_id[32];
    bool active;
} ws_client_t;

static ws_client_t s_clients[MIMI_WS_MAX_CLIENTS];

// 获取互斥锁
static inline void clients_lock(void)
{
    if (s_clients_mutex) xSemaphoreTake(s_clients_mutex, portMAX_DELAY);
}

// 释放互斥锁
static inline void clients_unlock(void)
{
    if (s_clients_mutex) xSemaphoreGive(s_clients_mutex);
}

// 获取客户端的 chat_id（线程安全）
static bool get_client_chat_id_by_fd(int fd, char *chat_id_out, size_t chat_id_size)
{
    bool found = false;
    clients_lock();
    for (int i = 0; i < MIMI_WS_MAX_CLIENTS; i++) {
        if (s_clients[i].active && s_clients[i].fd == fd) {
            strncpy(chat_id_out, s_clients[i].chat_id, chat_id_size - 1);
            chat_id_out[chat_id_size - 1] = '\0';
            found = true;
            break;
        }
    }
    clients_unlock();
    return found;
}

// 更新客户端的 chat_id（线程安全）
static bool update_client_chat_id(int fd, const char *new_chat_id)
{
    bool found = false;
    clients_lock();
    for (int i = 0; i < MIMI_WS_MAX_CLIENTS; i++) {
        if (s_clients[i].active && s_clients[i].fd == fd) {
            strncpy(s_clients[i].chat_id, new_chat_id, sizeof(s_clients[i].chat_id) - 1);
            s_clients[i].chat_id[sizeof(s_clients[i].chat_id) - 1] = '\0';
            found = true;
            break;
        }
    }
    clients_unlock();
    return found;
}

// 添加客户端（线程安全）
static bool add_client(int fd)
{
    bool success = false;
    clients_lock();
    for (int i = 0; i < MIMI_WS_MAX_CLIENTS; i++) {
        if (!s_clients[i].active) {
            s_clients[i].fd = fd;
            snprintf(s_clients[i].chat_id, sizeof(s_clients[i].chat_id), "ws_%d", fd);
            s_clients[i].active = true;
            ESP_LOGI(TAG, "Client connected: %s (fd=%d)", s_clients[i].chat_id, fd);
            success = true;
            break;
        }
    }
    if (!success) {
        ESP_LOGW(TAG, "Max clients reached, rejecting fd=%d", fd);
    }
    clients_unlock();
    return success;
}

// 移除客户端（线程安全）
static void remove_client(int fd)
{
    clients_lock();
    for (int i = 0; i < MIMI_WS_MAX_CLIENTS; i++) {
        if (s_clients[i].active && s_clients[i].fd == fd) {
            ESP_LOGI(TAG, "Client disconnected: %s", s_clients[i].chat_id);
            s_clients[i].active = false;
            break;
        }
    }
    clients_unlock();
}

// 发送消息给指定 chat_id 的客户端（线程安全）
static esp_err_t send_to_client(const char *chat_id, const char *text)
{
    if (!s_server) return ESP_ERR_INVALID_STATE;

    int target_fd = -1;
    
    // 在锁内查找客户端
    clients_lock();
    for (int i = 0; i < MIMI_WS_MAX_CLIENTS; i++) {
        if (s_clients[i].active && strcmp(s_clients[i].chat_id, chat_id) == 0) {
            target_fd = s_clients[i].fd;
            break;
        }
    }
    clients_unlock();
    
    if (target_fd < 0) {
        ESP_LOGW(TAG, "No WS client with chat_id=%s", chat_id);
        return ESP_ERR_NOT_FOUND;
    }

    /* Build response JSON */
    cJSON *resp = cJSON_CreateObject();
    cJSON_AddStringToObject(resp, "type", "response");
    cJSON_AddStringToObject(resp, "content", text);
    cJSON_AddStringToObject(resp, "chat_id", chat_id);

    char *json_str = cJSON_PrintUnformatted(resp);
    cJSON_Delete(resp);

    if (!json_str) return ESP_ERR_NO_MEM;

    httpd_ws_frame_t ws_pkt = {
        .type = HTTPD_WS_TYPE_TEXT,
        .payload = (uint8_t *)json_str,
        .len = strlen(json_str),
    };

    esp_err_t ret = httpd_ws_send_frame_async(s_server, target_fd, &ws_pkt);
    free(json_str);

    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to send to %s: %s", chat_id, esp_err_to_name(ret));
        remove_client(target_fd);
    }

    return ret;
}

static esp_err_t ws_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET) {
        /* WebSocket handshake — register client */
        int fd = httpd_req_to_sockfd(req);
        add_client(fd);
        return ESP_OK;
    }

    /* Receive WebSocket frame */
    httpd_ws_frame_t ws_pkt = {0};
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;

    /* Get frame length */
    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
    if (ret != ESP_OK) return ret;

    if (ws_pkt.len == 0) return ESP_OK;

    ws_pkt.payload = calloc(1, ws_pkt.len + 1);
    if (!ws_pkt.payload) return ESP_ERR_NO_MEM;

    ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
    if (ret != ESP_OK) {
        free(ws_pkt.payload);
        return ret;
    }

    int fd = httpd_req_to_sockfd(req);
    char client_chat_id[32] = "ws_unknown";
    bool has_client = get_client_chat_id_by_fd(fd, client_chat_id, sizeof(client_chat_id));

    /* Parse JSON message */
    cJSON *root = cJSON_Parse((char *)ws_pkt.payload);
    free(ws_pkt.payload);

    if (!root) {
        ESP_LOGW(TAG, "Invalid JSON from fd=%d", fd);
        return ESP_OK;
    }

    cJSON *type = cJSON_GetObjectItem(root, "type");
    cJSON *content = cJSON_GetObjectItem(root, "content");

    if (type && cJSON_IsString(type) && strcmp(type->valuestring, "message") == 0
        && content && cJSON_IsString(content)) {

        /* Determine chat_id */
        const char *chat_id = has_client ? client_chat_id : "ws_unknown";
        cJSON *cid = cJSON_GetObjectItem(root, "chat_id");
        if (cid && cJSON_IsString(cid)) {
            chat_id = cid->valuestring;
            /* Update client's chat_id if provided */
            if (has_client) {
                update_client_chat_id(fd, chat_id);
            }
        }

        ESP_LOGI(TAG, "WS message from %s: %.40s...", chat_id, content->valuestring);

        /* Push to inbound bus */
        mimi_msg_t msg = {0};
        strncpy(msg.channel, MIMI_CHAN_WEBSOCKET, sizeof(msg.channel) - 1);
        strncpy(msg.chat_id, chat_id, sizeof(msg.chat_id) - 1);
        msg.content = strdup(content->valuestring);
        if (msg.content) {
            message_bus_push_inbound(&msg);
        }
    }

    cJSON_Delete(root);
    return ESP_OK;
}

esp_err_t ws_server_start(void)
{
    memset(s_clients, 0, sizeof(s_clients));

    // 创建客户端列表互斥锁
    if (!s_clients_mutex) {
        s_clients_mutex = xSemaphoreCreateMutex();
        if (!s_clients_mutex) {
            ESP_LOGE(TAG, "Failed to create clients mutex");
            return ESP_ERR_NO_MEM;
        }
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = MIMI_WS_PORT;
    config.ctrl_port = MIMI_WS_PORT + 1;
    config.max_open_sockets = MIMI_WS_MAX_CLIENTS;

    esp_err_t ret = httpd_start(&s_server, &config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start WebSocket server: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Register WebSocket URI */
    httpd_uri_t ws_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = ws_handler,
        .is_websocket = true,
    };
    httpd_register_uri_handler(s_server, &ws_uri);

    ESP_LOGI(TAG, "WebSocket server started on port %d", MIMI_WS_PORT);
    return ESP_OK;
}

esp_err_t ws_server_send(const char *chat_id, const char *text)
{
    return send_to_client(chat_id, text);
}

esp_err_t ws_server_stop(void)
{
    if (s_server) {
        httpd_stop(s_server);
        s_server = NULL;
        ESP_LOGI(TAG, "WebSocket server stopped");
    }
    
    // 删除客户端列表互斥锁
    if (s_clients_mutex) {
        vSemaphoreDelete(s_clients_mutex);
        s_clients_mutex = NULL;
    }
    
    return ESP_OK;
}
