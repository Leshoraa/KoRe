/**
 * @file http_server.h
 * @brief Embedded asynchronous HTTP web server, JSON telemetry, and MJPEG streamer.
 */

#ifndef HTTP_SERVER_H
#define HTTP_SERVER_H

#include "include/kore_config.h"
#include "esp_http_server.h"

#ifdef __cplusplus
extern "C" {
#endif

extern httpd_handle_t g_stream_httpd;
extern httpd_handle_t g_camera_httpd;

void startWebServer(void);

#ifdef __cplusplus
}
#endif

#endif /* HTTP_SERVER_H */
