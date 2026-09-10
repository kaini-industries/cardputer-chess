#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>
extern uint32_t auditNow;
inline uint32_t millis() { return auditNow; }
inline void delay(uint32_t ms) { auditNow += ms; }
