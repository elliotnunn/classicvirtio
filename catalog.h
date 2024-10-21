#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "9p.h"

void CatalogInit(struct Qid9 root);
int32_t CatalogWalk(uint32_t fid, int32_t cnid, const unsigned char *paspath, int32_t *retparent, char *retname);
void CatalogSet(int32_t cnid, int32_t pcnid, const char *name, bool nameDefinitive);
int32_t CatalogGet(int32_t cnid, char *retname);
bool IsErr(int32_t cnid);
bool IsDir(int32_t cnid);
int32_t QID2CNID(struct Qid9 qid);
