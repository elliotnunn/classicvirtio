/* Copyright (c) 2024 Elliot Nunn */
/* Licensed under the MIT license */

// Each device independently searches the system for a Virtio console device,
// and uses its "emergency write" facility

#pragma once
void InitLog(void); // populates the following global...
extern bool LogEnable; // whether to bother expensive prep for printf calls
extern char LogPrefix[32]; // line prefix
