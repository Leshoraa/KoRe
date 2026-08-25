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
 * @brief Check if a central device (phone) is currently connected via BLE.
 * @return true if connected, false otherwise.
 */
bool isBleConnected(void);

#ifdef __cplusplus
}
#endif

#endif /* BLE_MANAGER_H */
