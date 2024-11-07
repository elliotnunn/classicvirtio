/* Copyright (c) Elliot Nunn */
/* Licensed under the MIT license */

#pragma once

#include <stdbool.h>

bool TAPResult(bool ok, const char *fmt, ...);
void TAPBailOut(const char *fmt, ...);
void TAPPlan(void);
