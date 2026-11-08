// Copyright (c) 2026 Dickson Chow (https://github.com/dicksondickson)
// SPDX-License-Identifier: MIT
#pragma once
#include <Arduino.h>

#include "app_config.h"

// Serial log (USB, 115200 baud), only when SERIAL_LOG is 1 in config.h.
// The default, 0, keeps release firmware silent (user, 2026-11-08). When off,
// the arguments aren't evaluated and nothing is compiled in.
#if SERIAL_LOG
#define LOGF(...) Serial.printf(__VA_ARGS__)
#define LOGLN(s) Serial.println(s)
#else
#define LOGF(...) ((void)0)
#define LOGLN(s) ((void)0)
#endif
