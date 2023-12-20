/* Copyright (c) 2023 Elliot Nunn */
/* Licensed under the MIT license */

#pragma once

void HTallocate(void);
void HTallocatelater(void);
void HTinstall(int tag, const void *key, short klen, const void *val, short vlen);
void *HTlookup(int tag, const void *key, short klen);
