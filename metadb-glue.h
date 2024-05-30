// The bread around the sqlite3 "sandwich"
#pragma once
#include "sqlite3.h"
extern sqlite3 *metadb; // Single db pointer that all code should use
