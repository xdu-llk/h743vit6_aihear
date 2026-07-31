#ifndef CAMERA_H
#define CAMERA_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CAMERA_RGB_WIDTH   240U
#define CAMERA_RGB_HEIGHT  240U
#define CAMERA_JPEG_MAX_BYTES (60U * 1024U)

typedef enum {
  CAMERA_CAPTURE_OK = 0,
  CAMERA_CAPTURE_UNAVAILABLE,
  CAMERA_CAPTURE_RGB_TIMEOUT,
  CAMERA_CAPTURE_JPEG_TIMEOUT,
  CAMERA_CAPTURE_JPEG_INVALID,
  CAMERA_CAPTURE_IO_ERROR
} CameraCaptureResult;

typedef void (*CameraServiceFn)(void);

/* Initialization is optional: an absent module is a normal headless state. */
bool Camera_Init(void);
bool Camera_IsAvailable(void);
uint16_t Camera_GetSensorId(void);

/* Captures one RGB frame for the TFT and one JPEG frame for upload. */
CameraCaptureResult Camera_CaptureEvent(uint32_t frame_timeout_ms,
                                        CameraServiceFn service);
const uint8_t *Camera_GetRgb565(size_t *length);
const uint8_t *Camera_GetJpeg(size_t *length);
const char *Camera_ResultName(CameraCaptureResult result);

#endif
