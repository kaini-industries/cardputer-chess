#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>
extern uint32_t auditNow;
// Optional clock for search tests. Null uses auditNow unchanged.
inline uint32_t (*auditMillisHook)() = nullptr;
inline uint32_t millis() { return auditMillisHook ? auditMillisHook() : auditNow; }
inline void delay(uint32_t ms) { auditNow += ms; }
