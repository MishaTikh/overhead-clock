// POSIX time helpers for building the host tests on Windows (the ESP32 and Linux have them).
#pragma once
#include <time.h>
#if defined(_WIN32)
#include <stdlib.h>
static inline struct tm* localtime_r(const time_t* t, struct tm* r) { return localtime_s(r, t) ? nullptr : r; }
static inline struct tm* gmtime_r(const time_t* t, struct tm* r) { return gmtime_s(r, t) ? nullptr : r; }
static inline int setenv(const char* name, const char* value, int) { return _putenv_s(name, value); }
#define tzset _tzset
#endif
