/* Copyright (c) 2023 Elliot Nunn */
/* Licensed under the MIT license */

// Implemented by device-[gpu|9p|etc].c

#pragma once

#include <stdint.h>

// Device has finished with a buffer
void DNotified(__attribute__((unused)) __attribute__((unused)) uint16_t q, const volatile uint32_t *retlen);

// Device-specific configuration struct has changed
void DConfigChange(void);
