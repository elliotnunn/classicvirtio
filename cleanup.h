/* Copyright (c) Elliot Nunn */
/* Licensed under the MIT license */

#pragma once

void RegisterCleanup(void (*function)(void));
void RegisterCleanupVoidPtr(void (*function)(void *), void *arg);
void RegisterCleanupCharPtr(void (*function)(char *), char *arg);
void Cleanup(void);
