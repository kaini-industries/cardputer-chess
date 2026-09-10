#pragma once
#include <cstdint>
inline uint32_t esp_random() { static uint32_t x=42; x=x*1664525u+1013904223u; return x; }
