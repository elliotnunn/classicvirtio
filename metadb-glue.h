// The bread around the sqlite3 "sandwich"
#pragma once
#include "sqlite3.h"
extern sqlite3 *metadb; // Single db pointer that all code should use

// A GCC statement-expr: evaluates to the static "stmt" variable
#define PERSISTENT_STMT(db, string) ({\
	static sqlite3_stmt *stmt; \
	if (stmt == NULL) { \
		int sqerr = sqlite3_prepare_v3(db, string, -1, SQLITE_PREPARE_PERSISTENT, &stmt, NULL); \
		if (sqerr != SQLITE_OK) { \
			panic(string); \
		} \
	} \
	stmt; \
})
