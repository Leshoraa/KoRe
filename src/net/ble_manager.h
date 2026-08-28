/**
 * @file ble_manager.h
 * @brief Bluetooth Low Energy (BLE) Nordic UART Service (NUS) GATT server for mobile notifications.
 */

#ifndef BLE_MANAGER_H
#define BLE_MANAGER_H

#include "include/kore_config.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize BLE GATT Server with Nordic UART Service (NUS).
 * Advertises BLE_DEVICE_NAME ("KoRe-Sense") and listens for incoming notification strings.
 */
void initBleNotificationServer(void);

/**
 * @brief Format standardized telemetry JSON payload across HTTP and BLE endpoints.
 * @param[out] json Destination buffer for JSON string.
 * @param[in] max_len Maximum capacity of destination buffer.
 */
void formatTelemetryJson(char *json, size_t max_len);

/**
 * @brief Transmit an immediate telemetry snapshot over BLE NUS TX characteristic.
 */
void sendBleTelemetryNow(void);

/**
 * @brief Configure periodic telemetry streaming over BLE GATT notifications.
 * @param[in] enable True to enable periodic streaming, false to disable.
 * @param[in] interval_ms Streaming period in milliseconds (minimum 100ms).
 */
void setBleTelemetryStreaming(bool enable, uint32_t interval_ms);

/**
 * @brief Query if periodic telemetry streaming is currently enabled.
 * @return True if streaming is active, false otherwise.
 */
bool isBleTelemetryStreaming(void);

#ifdef __cplusplus
}
#endif

#endif /* BLE_MANAGER_H */
