#pragma once

void CatalogInit(void);
int32_t browse(uint32_t fid, int32_t cnid, const unsigned char *paspath);
static const char *getDBName(int32_t cnid);
static int32_t getDBParent(int32_t cnid);
static void setDB(int32_t cnid, int32_t pcnid, const char *name);
