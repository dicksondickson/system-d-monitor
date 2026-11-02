#pragma once
// Pulls in include/config.h, or the example (Wi-Fi off) if it's missing.
#if __has_include("config.h")
#include "config.h"
#define CONFIG_PRESENT 1
#else
#warning "include/config.h not found: copy include/config.example.h and fill it in. Building with Wi-Fi disabled."
#include "config.example.h"
#define CONFIG_PRESENT 0
#endif
