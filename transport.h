/* Copyright (c) 2023 Elliot Nunn */
/* Licensed under the MIT license */

// Implemented by transport-*.c

#pragma once

#include <NameRegistry.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// The void pointer somehow identifies the PCI or MMIO device
// returns true for OK
bool VInit(RegEntryID *dev);
bool VFinal(RegEntryID *dev);

// Sets these globals
extern void *VConfig;

// Negotiate features
bool VGetDevFeature(uint32_t number);
void VSetFeature(uint32_t number, bool val);
bool VFeaturesOK(void);
void VDriverOK(void);
void VFail(void);

// Tell the device where to find the three (split) virtqueue rings
uint16_t VQueueMaxSize(uint16_t q);
void VQueueSet(uint16_t q, uint16_t size, uint32_t desc, uint32_t avail, uint32_t used);

// Tell the device about a change to the avail ring
void VNotify(uint16_t queue);
