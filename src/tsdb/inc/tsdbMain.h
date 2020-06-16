/*
 * Copyright (c) 2019 TAOS Data, Inc. <jhtao@taosdata.com>
 *
 * This program is free software: you can use, redistribute, and/or modify
 * it under the terms of the GNU Affero General Public License, version 3
 * or later ("AGPL"), as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */
#ifndef _TD_TSDB_MAIN_H_
#define _TD_TSDB_MAIN_H_

#include "hash.h"
#include "tcoding.h"
#include "tglobal.h"
#include "tkvstore.h"
#include "tlist.h"
#include "tlog.h"
#include "tref.h"
#include "tsdb.h"
#include "tskiplist.h"
#include "tutil.h"

#ifdef __cplusplus
extern "C" {
#endif

extern int tsdbDebugFlag;

#define tsdbError(...) { if (tsdbDebugFlag & DEBUG_ERROR) { taosPrintLog("ERROR TDB ", tsdbDebugFlag, __VA_ARGS__); }}
#define tsdbWarn(...)  { if (tsdbDebugFlag & DEBUG_WARN)  { taosPrintLog("WARN TDB ", tsdbDebugFlag, __VA_ARGS__); }}
#define tsdbTrace(...) { if (tsdbDebugFlag & DEBUG_TRACE) { taosPrintLog("TDB ", tsdbDebugFlag, __VA_ARGS__); }}
#define tsdbPrint(...) { taosPrintLog("TDB ", 255, __VA_ARGS__); }

#define TSDB_MAX_TABLE_SCHEMAS 16
#define TSDB_FILE_HEAD_SIZE 512
#define TSDB_FILE_DELIMITER 0xF00AFA0F

// Definitions
// ------------------ tsdbMeta.c
typedef struct STable {
  ETableType     type;
  tstr*          name;  // NOTE: there a flexible string here
  STableId       tableId;
  uint64_t       suid;
  struct STable* pSuper;  // super table pointer
  uint8_t        numOfSchemas;
  STSchema       schema[TSDB_MAX_TABLE_SCHEMAS];
  STSchema*      tagSchema;
  SKVRow         tagVal;
  void*          pIndex;         // For TSDB_SUPER_TABLE, it is the skiplist index
  void*          eventHandler;   // TODO
  void*          streamHandler;  // TODO
  TSKEY          lastKey;        // lastkey inserted in this table, initialized as 0, TODO: make a structure
  char*          sql;
  void*          cqhandle;
  T_REF_DECLARE();
} STable;

typedef struct {
  pthread_rwlock_t rwLock;

  int32_t   nTables;
  STable**  tables;
  SList*    superList;
  SHashObj* uidMap;
  SKVStore* pStore;
} STsdbMeta;

// ------------------ tsdbBuffer.c
typedef struct {
  int64_t blockId;
  int     offset;
  int     remain;
  char    data[];
} STsdbBufBlock;

typedef struct {
  pthread_cond_t poolNotEmpty;
  int            bufBlockSize;
  int            tBufBlocks;
  int            nBufBlocks;
  int64_t        index;
  SList*         bufBlockList;
} STsdbBufPool;

// ------------------ tsdbMemTable.c
typedef struct {
  uint64_t   uid;
  TSKEY      keyFirst;
  TSKEY      keyLast;
  int64_t    numOfRows;
  SSkipList* pData;
} STableData;

typedef struct {
  T_REF_DECLARE();
  TSKEY        keyFirst;
  TSKEY        keyLast;
  int64_t      numOfRows;
  STableData** tData;
  SList*       actList;
  SList*       bufBlockList;
  int          maxCols;
  int          maxRowBytes;
} SMemTable;

// ------------------ tsdbFile.c
extern const char* tsdbFileSuffix[];
typedef enum {
  TSDB_FILE_TYPE_HEAD = 0,
  TSDB_FILE_TYPE_DATA,
  TSDB_FILE_TYPE_LAST,
  TSDB_FILE_TYPE_NHEAD,
  TSDB_FILE_TYPE_NLAST
} TSDB_FILE_TYPE;

typedef struct {
  uint32_t offset;
  uint32_t len;
  uint64_t size;      // total size of the file
  uint64_t tombSize;  // unused file size
  uint32_t totalBlocks;
  uint32_t totalSubBlocks;
} STsdbFileInfo;

typedef struct {
  char* fname;
  int   fd;

  STsdbFileInfo info;
} SFile;

typedef struct {
  int fileId;
  SFile headF;
  SFile dataF;
  SFile lastF;
} SFileGroup;

typedef struct {
  pthread_rwlock_t fhlock;

  int         maxFGroups;
  int         nFGroups;
  SFileGroup* pFGroup;
} STsdbFileH;

typedef struct {
  int         numOfFGroups;
  SFileGroup *base;
  SFileGroup *pFileGroup;
  int         direction;
} SFileGroupIter;

// ------------------ tsdbMain.c
typedef struct {
  int8_t state;

  char*           rootDir;
  STsdbCfg        config;
  STsdbAppH       appH;
  STsdbStat       stat;
  STsdbMeta*      tsdbMeta;
  STsdbBufPool*   pPool;
  SMemTable*      mem;
  SMemTable*      imem;
  STsdbFileH*     tsdbFileH;
  int             commit;
  pthread_t       commitThread;
  pthread_mutex_t mutex;
  bool            repoLocked;
} STsdbRepo;

// ------------------ tsdbRWHelper.c
typedef struct {
  uint32_t len;
  uint32_t offset;
  // uint32_t padding;
  uint32_t hasLast : 2;
  uint32_t numOfBlocks : 30;
  uint64_t uid;
  TSKEY    maxKey;
} SCompIdx;

typedef struct {
  int64_t last : 1;
  int64_t offset : 63;
  int32_t algorithm : 8;
  int32_t numOfRows : 24;
  int32_t sversion;
  int32_t len;
  int16_t numOfSubBlocks;
  int16_t numOfCols;
  TSKEY   keyFirst;
  TSKEY   keyLast;
} SCompBlock;

typedef struct {
  int32_t    delimiter;  // For recovery usage
  int32_t    checksum;   // TODO: decide if checksum logic in this file or make it one API
  uint64_t   uid;
  SCompBlock blocks[];
} SCompInfo;

typedef struct {
  int16_t colId;
  int16_t len;
  int32_t type : 8;
  int32_t offset : 24;
  int64_t sum;
  int64_t max;
  int64_t min;
  int16_t maxIndex;
  int16_t minIndex;
  int16_t numOfNull;
  char    padding[2];
} SCompCol;

typedef struct {
  int32_t  delimiter;  // For recovery usage
  int32_t  numOfCols;  // For recovery usage
  uint64_t uid;        // For recovery usage
  SCompCol cols[];
} SCompData;

typedef enum { TSDB_WRITE_HELPER, TSDB_READ_HELPER } tsdb_rw_helper_t;

typedef struct {
  int   fid;
  TSKEY minKey;
  TSKEY maxKey;
  // For read/write purpose
  SFile headF;
  SFile dataF;
  SFile lastF;
  // For write purpose only
  SFile nHeadF;
  SFile nLastF;
} SHelperFile;

typedef struct {
  uint64_t uid;
  int32_t  tid;
  int32_t  sversion;
} SHelperTable;

typedef struct {
  tsdb_rw_helper_t type;

  STsdbRepo* pRepo;
  int8_t     state;
  // For file set usage
  SHelperFile files;
  SCompIdx*   pCompIdx;
  // For table set usage
  SHelperTable tableInfo;
  SCompInfo*   pCompInfo;
  bool         hasOldLastBlock;
  // For block set usage
  SCompData* pCompData;
  SDataCols* pDataCols[2];
  void*      pBuffer;     // Buffer to hold the whole data block
  void*      compBuffer;  // Buffer for temperary compress/decompress purpose
} SRWHelper;


// Operations
// ------------------ tsdbMeta.c
#define TABLE_TYPE(t) (t)->type
#define TABLE_NAME(t) (t)->name
#define TABLE_CHAR_NAME(t) TABLE_NAME(t)->data
#define TALBE_UID(t) (t)->tableId.uid
#define TABLE_TID(t) (t)->tableId.tid
#define TABLE_SUID(t) (t)->suid
#define TABLE_LASTKEY(t) (t)->lastKey

STsdbMeta* tsdbNewMeta(STsdbCfg* pCfg);
void       tsdbFreeMeta(STsdbMeta* pMeta);
int        tsdbOpenMeta(STsdbRepo* pRepo);
int        tsdbCloseMeta(STsdbRepo* pRepo);
STSchema*  tsdbGetTableSchema(STable* pTable);
STable*    tsdbGetTableByUid(STsdbMeta* pMeta, uint64_t uid);
STSchema*  tsdbGetTableSchemaByVersion(STable* pTable, int16_t version);
STSchema*  tsdbGetTableTagSchema(STable* pTable);
int        tsdbUpdateTable(STsdbMeta* pMeta, STable* pTable, STableCfg* pCfg);
int        tsdbWLockRepoMeta(STsdbRepo* pRepo);
int        tsdbRLockRepoMeta(STsdbRepo* pRepo);
int        tsdbUnlockRepoMeta(STsdbRepo* pRepo);
void       tsdbRefTable(STable* pTable);
void       tsdbUnRefTable(STable* pTable);

// ------------------ tsdbBuffer.c
STsdbBufPool* tsdbNewBufPool();
void          tsdbFreeBufPool(STsdbBufPool* pBufPool);
int           tsdbOpenBufPool(STsdbRepo* pRepo);
void          tsdbCloseBufPool(STsdbRepo* pRepo);
SListNode*    tsdbAllocBufBlockFromPool(STsdbRepo* pRepo);

// ------------------ tsdbMemTable.c
int tsdbInsertRowToMem(STsdbRepo* pRepo, SDataRow row, STable* pTable);
int tsdbRefMemTable(STsdbRepo* pRepo, SMemTable* pMemTable);
int tsdbUnRefMemTable(STsdbRepo* pRepo, SMemTable* pMemTable);
int tsdbTakeMemSnapshot(STsdbRepo* pRepo, SMemTable** pMem, SMemTable** pIMem);

// ------------------ tsdbFile.c
#define TSDB_KEY_FILEID(key, daysPerFile, precision) ((key) / tsMsPerDay[(precision)] / (daysPerFile))
#define TSDB_MAX_FILE(keep, daysPerFile) ((keep) / (daysPerFile) + 3)
#define TSDB_MIN_FILE_ID(fh) (fh)->pFGroup[0].fileId
#define TSDB_MAX_FILE_ID(fh) (fh)->pFGroup[(fh)->nFGroups - 1].fileId
#define TSDB_FGROUP_ITER_FORWARD TSDB_ORDER_ASC
#define TSDB_FGROUP_ITER_BACKWARD TSDB_ORDER_DESC

STsdbFileH* tsdbNewFileH(STsdbCfg* pCfg);
void        tsdbFreeFileH(STsdbFileH* pFileH);

// ------------------ tsdbRWHelper.c
#define TSDB_HELPER_CLEAR_STATE 0x0        // Clear state
#define TSDB_HELPER_FILE_SET_AND_OPEN 0x1  // File is set
#define TSDB_HELPER_IDX_LOAD 0x2           // SCompIdx part is loaded
#define TSDB_HELPER_TABLE_SET 0x4          // Table is set
#define TSDB_HELPER_INFO_LOAD 0x8          // SCompInfo part is loaded
#define TSDB_HELPER_FILE_DATA_LOAD 0x10    // SCompData part is loaded
#define helperSetState(h, s) (((h)->state) |= (s))
#define helperClearState(h, s) ((h)->state &= (~(s)))
#define helperHasState(h, s) ((((h)->state) & (s)) == (s))
#define blockAtIdx(h, idx) ((h)->pCompInfo->blocks + idx)
#define TSDB_MAX_SUBBLOCKS 8
#define IS_SUB_BLOCK(pBlock) ((pBlock)->numOfSubBlocks == 0)
#define helperType(h) (h)->type
#define helperRepo(h) (h)->pRepo
#define helperState(h) (h)->state

// ------------------ tsdbMain.c
#define REPO_ID(r) (r)->config.tsdbId
#define IS_REPO_LOCKED(r) (r)->repoLocked

char* tsdbGetMetaFileName(char* rootDir);
char* tsdbGetDataFileName(STsdbRepo* pRepo, int fid, int type);
int   tsdbLockRepo(STsdbRepo* pRepo);
int   tsdbUnlockRepo(STsdbRepo* pRepo);

#if 0

// --------- Helper state


int  tsdbInitReadHelper(SRWHelper *pHelper, STsdbRepo *pRepo);
int  tsdbInitWriteHelper(SRWHelper *pHelper, STsdbRepo *pRepo);
void tsdbDestroyHelper(SRWHelper *pHelper);
void tsdbResetHelper(SRWHelper *pHelper);

// --------- For set operations
int tsdbSetAndOpenHelperFile(SRWHelper *pHelper, SFileGroup *pGroup);
void tsdbSetHelperTable(SRWHelper *pHelper, STable *pTable, STsdbRepo *pRepo);
int  tsdbCloseHelperFile(SRWHelper *pHelper, bool hasError);

// --------- For read operations
int  tsdbLoadCompIdx(SRWHelper *pHelper, void *target);
int  tsdbLoadCompInfo(SRWHelper *pHelper, void *target);
int  tsdbLoadCompData(SRWHelper *pHelper, SCompBlock *pCompBlock, void *target);
int  tsdbLoadBlockDataCols(SRWHelper *pHelper, SDataCols *pDataCols, int blkIdx, int16_t *colIds, int numOfColIds);
int  tsdbLoadBlockData(SRWHelper *pHelper, SCompBlock *pCompBlock, SDataCols *target);
void tsdbGetDataStatis(SRWHelper *pHelper, SDataStatis *pStatis, int numOfCols);

// --------- For write operations
int tsdbWriteDataBlock(SRWHelper *pHelper, SDataCols *pDataCols);
int tsdbMoveLastBlockIfNeccessary(SRWHelper *pHelper);
int tsdbWriteCompInfo(SRWHelper *pHelper);
int tsdbWriteCompIdx(SRWHelper *pHelper);

// --------- Other functions need to further organize
void      tsdbFitRetention(STsdbRepo *pRepo);
int       tsdbAlterCacheTotalBlocks(STsdbRepo *pRepo, int totalBlocks);
void      tsdbAdjustCacheBlocks(STsdbCache *pCache);
int32_t   tsdbGetMetaFileName(char *rootDir, char *fname);
int       tsdbUpdateFileHeader(SFile *pFile, uint32_t version);


int compFGroupKey(const void *key, const void *fgroup);
#endif

#ifdef __cplusplus
}
#endif

#endif