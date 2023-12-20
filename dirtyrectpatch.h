/* Copyright (c) 2023 Elliot Nunn */
/* Licensed under the MIT license */

#pragma once

// Implemented by the callee
void InstallDirtyRectPatch(void);

// Implemented by the caller
void DirtyRectCallback(short top, short left, short bottom, short right);
