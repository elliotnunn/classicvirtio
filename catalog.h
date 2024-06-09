#pragma once

void CatalogInit(const char *rootname);
int32_t CatalogWalk(uint32_t fid, int32_t cnid, const unsigned char *paspath);
const char *getDBName(int32_t cnid);
int32_t getDBParent(int32_t cnid);
void setDB(int32_t cnid, int32_t pcnid, const char *name);
