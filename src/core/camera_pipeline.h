/**
 * @file camera_pipeline.h
 * @brief High-speed YCbCr vision pipeline, spatial clustering, and FreeRTOS Core 0 camera task.
 */

#ifndef CAMERA_PIPELINE_H
#define CAMERA_PIPELINE_H

#include "include/kore_config.h"
#include "include/kore_types.h"
#include "esp_camera.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initializes OV2640 / OV3660 camera hardware driver settings.
 * @return True if initialized successfully, false otherwise.
 */
bool initCamera(void);

/**
 * @brief Toggles camera sensor software standby via SCCB register writes.
 * @param[in] enable True to power down sensor core; false to wake up.
 */
void setCameraSleep(bool enable);

/**
 * @brief Allocates vision working arrays in internal SRAM and stream buffer in PSRAM.
 * @return True if allocations succeeded.
 */
bool allocateVisionBuffers(void);

/**
 * @brief Master FreeRTOS vision processing and power management task pinned to Core 0.
 */
void cameraTask(void *pvParameters);

#ifdef __cplusplus
}
#endif

#endif /* CAMERA_PIPELINE_H */
