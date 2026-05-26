#pragma once
#include <Arduino.h>
#include <stdio.h>

// Drop-in replacement for esp_log.h on non-ESP32 targets.
// Renesas UART has no printf, so we format into a stack buffer first.
#define _LOG(lvl, tag, fmt, ...) do { \
  char _buf[384]; \
  snprintf(_buf, sizeof(_buf), "[" lvl "][%s] " fmt "\r\n", tag, ##__VA_ARGS__); \
  Serial.print(_buf); \
} while(0)

#define LOG_I(tag, fmt, ...) _LOG("I", tag, fmt, ##__VA_ARGS__)
#define LOG_W(tag, fmt, ...) _LOG("W", tag, fmt, ##__VA_ARGS__)
#define LOG_E(tag, fmt, ...) _LOG("E", tag, fmt, ##__VA_ARGS__)
#if defined(CORE_DEBUG_LEVEL) && CORE_DEBUG_LEVEL >= 3
  #define LOG_D(tag, fmt, ...) _LOG("D", tag, fmt, ##__VA_ARGS__)
#else
  #define LOG_D(tag, fmt, ...) do {} while(0)
#endif
