/* Copyright (c) Elliot Nunn */
/* Licensed under the MIT license */

#pragma once
#include <stdint.h>
#include "9p.h"
int32_t ReadDirSorted(uint32_t navfid, int32_t pcnid, int16_t index, bool dirOK, char retname[MAXNAME]);
