/* Copyright (c) 2023 Elliot Nunn */
/* Licensed under the MIT license */

// Implemented by device-[gpu|9p|etc].c

#pragma once

#include <stdint.h>

// Device has finished with a buffer
void DNotified(uint16_t q, volatile uint32_t *retlen);

// Device-specific configuration struct has changed
void DConfigChange(void);
