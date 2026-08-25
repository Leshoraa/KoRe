/**
 * @file notification_client.h
 * @brief Ntfy.sh background cloud push client and local notification dispatcher.
 */

#ifndef NOTIFICATION_CLIENT_H
#define NOTIFICATION_CLIENT_H

#include "include/kore_config.h"
#include "include/kore_types.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void initNotificationClient(void);
bool setNtfyTopic(const char* topic);
const char* getNtfyTopic(void);
bool isNtfyConnected(void);
uint32_t getNtfyLastMessageTime(void);
void pushLocalNotification(const char* app, const char* title, const char* message);

#ifdef __cplusplus
}
#endif

#endif /* NOTIFICATION_CLIENT_H */
