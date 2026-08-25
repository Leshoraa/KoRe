/**
 * @file display_engine.h
 * @brief LovyanGFX SSD1306 display driver and rigid facial rig composition engine.
 */

#ifndef DISPLAY_ENGINE_H
#define DISPLAY_ENGINE_H

#include "include/kore_config.h"
#include "include/kore_types.h"
#include <LovyanGFX.hpp>

/**
 * @class LGFX
 * @brief SSD1306 display driver subclass configured for 1.0 MHz Fast-Mode Plus I2C bus.
 */
class LGFX : public lgfx::LGFX_Device {
    lgfx::Panel_SSD1306 _panel_instance;
    lgfx::Bus_I2C _bus_instance;

public:
    LGFX() {
        {
            auto cfg = _bus_instance.config();
            cfg.i2c_port = OLED_I2C_PORT;
            cfg.freq_write = OLED_I2C_FREQ_WRITE_HZ;
            cfg.freq_read = OLED_I2C_FREQ_READ_HZ;
            cfg.pin_scl = PIN_OLED_SCL;
            cfg.pin_sda = PIN_OLED_SDA;
            _bus_instance.config(cfg);
            _panel_instance.setBus(&_bus_instance);
        }
        {
            auto cfg = _panel_instance.config();
            cfg.panel_width = OLED_PANEL_WIDTH_PX;
            cfg.panel_height = OLED_PANEL_HEIGHT_PX;
            cfg.offset_x = 0;
            cfg.offset_y = 0;
            _panel_instance.config(cfg);
        }
        setPanel(&_panel_instance);
    }
};

extern LGFX lcd;
extern LGFX_Sprite canvas;

enum AmbientScreenMode {
    AMBIENT_NONE = 0,
    AMBIENT_CLOCK,
    AMBIENT_WEATHER,
    AMBIENT_NOTIFICATION
};

void showBootStatus(const char* line1, const char* line2 = nullptr);
void drawFace(Expression expr, float eyeHeightFactor, float offsetX, float offsetY, float frame = 0.0f, float vergence = 0.0f, float scale = 1.0f);
void drawClockScreen(float animFrame);
void drawWeatherScreen(const WeatherInfo& weather, float animFrame);
void drawNotificationScreen(const NotificationInfo& notif, float animFrame);
void triggerAmbientDisplay(AmbientScreenMode mode, uint32_t duration_ms = WEATHER_POPUP_DURATION_MS);
void triggerWeatherDisplay(uint32_t duration_ms = WEATHER_POPUP_DURATION_MS);
void triggerClockDisplay(uint32_t duration_ms = WEATHER_POPUP_DURATION_MS);
void triggerNotificationDisplay(const NotificationInfo& notif, uint32_t duration_ms = NOTIFICATION_POPUP_DURATION_MS);
void transitionToAmbient(AmbientScreenMode toMode, float durationMs = 160.0f);
void transitionFromAmbientToFace(AmbientScreenMode fromMode, Expression toExpr, float durationMs = 140.0f);
void setOledBrightnessLive(uint8_t brightness);
void transitionExpression(Expression fromExpr, Expression toExpr, float durationMs = 170.0f);
void oledTask(void *pvParameters);

#endif /* DISPLAY_ENGINE_H */
