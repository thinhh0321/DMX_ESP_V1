/**
 * @file mod_web_routes.c
 * @brief URI Route Registration
 * 
 * Registers all HTTP endpoints and WebSocket handlers.
 */

#include "mod_web_routes.h"
#include "mod_web_static.h"
#include "mod_web_api.h"
#include "mod_web_ws.h"
#include "esp_log.h"

static const char *TAG = "MOD_WEB_ROUTES";

/* ========== PUBLIC API ========== */

esp_err_t mod_web_register_routes(httpd_handle_t server)
{
    esp_err_t ret;

    // ========== Static File Handlers ==========
    
    // GET / -> index.html.gz
    httpd_uri_t uri_index = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = mod_web_static_handler_index,
        .user_ctx = NULL
    };
    ret = httpd_register_uri_handler(server, &uri_index);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register / handler");
        return ret;
    }

    // GET /app.js -> app.js.gz
    httpd_uri_t uri_app_js = {
        .uri = "/app.js",
        .method = HTTP_GET,
        .handler = mod_web_static_handler_js,
        .user_ctx = NULL
    };
    ret = httpd_register_uri_handler(server, &uri_app_js);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register /app.js handler");
        return ret;
    }

    // GET /style.css -> style.css.gz
    httpd_uri_t uri_style_css = {
        .uri = "/style.css",
        .method = HTTP_GET,
        .handler = mod_web_static_handler_css,
        .user_ctx = NULL
    };
    ret = httpd_register_uri_handler(server, &uri_style_css);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register /style.css handler");
        return ret;
    }

    // ========== System API Handlers ==========
    // Per MOD_WEB.md: GET /api/sys/info, POST /api/dmx/config, POST /api/net/config
    
    // GET /api/sys/info
    httpd_uri_t uri_sys_info = {
        .uri = "/api/sys/info",
        .method = HTTP_GET,
        .handler = mod_web_api_system_info,
        .user_ctx = NULL
    };
    ret = httpd_register_uri_handler(server, &uri_sys_info);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register /api/sys/info handler");
        return ret;
    }

    // POST /api/auth/login (new)
    httpd_uri_t uri_auth_login = {
        .uri = "/api/auth/login",
        .method = HTTP_POST,
        .handler = mod_web_api_auth_login,
        .user_ctx = NULL
    };
    ret = httpd_register_uri_handler(server, &uri_auth_login);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register /api/auth/login handler");
        return ret;
    }

    // POST /api/auth/set_password (initial setup or change)
    httpd_uri_t uri_auth_setpw = {
        .uri = "/api/auth/set_password",
        .method = HTTP_POST,
        .handler = mod_web_api_auth_set_password,
        .user_ctx = NULL
    };
    ret = httpd_register_uri_handler(server, &uri_auth_setpw);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register /api/auth/set_password handler");
        return ret;
    }

    // POST /api/sys/reboot (additional endpoint)
    httpd_uri_t uri_sys_reboot = {
        .uri = "/api/sys/reboot",
        .method = HTTP_POST,
        .handler = mod_web_api_system_reboot,
        .user_ctx = NULL
    };
    ret = httpd_register_uri_handler(server, &uri_sys_reboot);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register /api/sys/reboot handler");
        return ret;
    }

    // POST /api/sys/factory (additional endpoint)
    httpd_uri_t uri_sys_factory = {
        .uri = "/api/sys/factory",
        .method = HTTP_POST,
        .handler = mod_web_api_system_factory,
        .user_ctx = NULL
    };
    ret = httpd_register_uri_handler(server, &uri_sys_factory);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register /api/sys/factory handler");
        return ret;
    }

    // ========== DMX API Handlers ==========
    
    // POST /api/dmx/config (per MOD_WEB.md)
    httpd_uri_t uri_dmx_config = {
        .uri = "/api/dmx/config",
        .method = HTTP_POST,
        .handler = mod_web_api_dmx_config,
        .user_ctx = NULL
    };
    ret = httpd_register_uri_handler(server, &uri_dmx_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register /api/dmx/config handler");
        return ret;
    }

    // GET /api/dmx/status (additional endpoint for status)
    httpd_uri_t uri_dmx_status = {
        .uri = "/api/dmx/status",
        .method = HTTP_GET,
        .handler = mod_web_api_dmx_status,
        .user_ctx = NULL
    };
    ret = httpd_register_uri_handler(server, &uri_dmx_status);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register /api/dmx/status handler");
        return ret;
    }

    // ========== Network API Handlers ==========
    
    // POST /api/net/config (per MOD_WEB.md)
    httpd_uri_t uri_net_config = {
        .uri = "/api/net/config",
        .method = HTTP_POST,
        .handler = mod_web_api_network_config,
        .user_ctx = NULL
    };
    ret = httpd_register_uri_handler(server, &uri_net_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register /api/net/config handler");
        return ret;
    }

    // GET /api/net/status (additional endpoint for status)
    httpd_uri_t uri_net_status = {
        .uri = "/api/net/status",
        .method = HTTP_GET,
        .handler = mod_web_api_network_status,
        .user_ctx = NULL
    };
    ret = httpd_register_uri_handler(server, &uri_net_status);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register /api/net/status handler");
        return ret;
    }

    // ========== File API Handlers ==========
    
    // GET /api/file/export
    httpd_uri_t uri_file_export = {
        .uri = "/api/file/export",
        .method = HTTP_GET,
        .handler = mod_web_api_file_export,
        .user_ctx = NULL
    };
    ret = httpd_register_uri_handler(server, &uri_file_export);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register /api/file/export handler");
        return ret;
    }

    // POST /api/file/import
    httpd_uri_t uri_file_import = {
        .uri = "/api/file/import",
        .method = HTTP_POST,
        .handler = mod_web_api_file_import,
        .user_ctx = NULL
    };
    ret = httpd_register_uri_handler(server, &uri_file_import);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register /api/file/import handler");
        return ret;
    }

    // ========== OTA Handler ==========
    
    // POST /api/sys/ota
    httpd_uri_t uri_sys_ota = {
        .uri = "/api/sys/ota",
        .method = HTTP_POST,
        .handler = mod_web_api_system_ota,
        .user_ctx = NULL
    };
    ret = httpd_register_uri_handler(server, &uri_sys_ota);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register /api/sys/ota handler");
        return ret;
    }

    // ========== Health/Telemetry Handler ==========
    
    // GET /api/sys/health
    httpd_uri_t uri_sys_health = {
        .uri = "/api/sys/health",
        .method = HTTP_GET,
        .handler = mod_web_api_system_health,
        .user_ctx = NULL
    };
    ret = httpd_register_uri_handler(server, &uri_sys_health);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register /api/sys/health handler");
        return ret;
    }

    // ========== WebSocket Handler ==========
    
    // GET /ws/status -> WebSocket upgrade
    httpd_uri_t uri_ws = {
        .uri = "/ws/status",
        .method = HTTP_GET,
        .handler = mod_web_ws_handler,
        .user_ctx = NULL,
        .is_websocket = true
    };
    ret = httpd_register_uri_handler(server, &uri_ws);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register /ws/status handler");
        return ret;
    }

    ESP_LOGI(TAG, "All routes registered successfully");
    return ESP_OK;
}

