#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include "sqlite3.h"

#if 0
#define BEGIN fprintf(stderr, "\nEnter %s\n", __func__);
#define RETURN(x) do{ fprintf(stderr, "%s returns %s at line %d\n", __func__, #x, __LINE__); return x; }while(0)
#else
#define BEGIN
#define RETURN(x) return x
#endif

typedef struct feosFile feosFile;
struct feosFile {
  sqlite3_file base;
  FILE *fp;
};

static int feosClose(sqlite3_file *pFile);
static int feosRead(sqlite3_file *pFile, void *zBuf, int iAmt, sqlite_int64 iOfst);
static int feosWrite(sqlite3_file *pFile, const void *zBuf, int iAmt, sqlite_int64 iOfst);
static int feosTruncate(sqlite3_file *pFile, sqlite_int64 size);
static int feosSync(sqlite3_file *pFile, int flags);
static int feosFileSize(sqlite3_file *pFile, sqlite_int64 *pSize);
static int feosLock(sqlite3_file *pFile, int eLock);
static int feosUnlock(sqlite3_file *pFile, int eLock);
static int feosCheckReservedLock(sqlite3_file *pFile, int *pResOut);
static int feosFileControl(sqlite3_file *pFile, int op, void *pArg);
static int feosSectorSize(sqlite3_file *pFile);
static int feosDeviceCharacteristics(sqlite3_file *pFile);

static const sqlite3_io_methods feosIO = {
  .iVersion               = 1,
  .xClose                 = feosClose,
  .xRead                  = feosRead,
  .xWrite                 = feosWrite,
  .xTruncate              = feosTruncate,
  .xSync                  = feosSync,
  .xFileSize              = feosFileSize,
  .xLock                  = feosLock,
  .xUnlock                = feosUnlock,
  .xCheckReservedLock     = feosCheckReservedLock,
  .xFileControl           = feosFileControl,
  .xSectorSize            = feosSectorSize,
  .xDeviceCharacteristics = feosDeviceCharacteristics,
};

static int feosOpen(sqlite3_vfs *zVfs, const char *zName, sqlite3_file *fp, int flags, int *pOutFlags);
static int feosDelete(sqlite3_vfs *zVfs, const char *zName, int syncDir);
static int feosAccess(sqlite3_vfs *zVfs, const char *zName, int flags, int *pResOut);
static int feosFullPathname(sqlite3_vfs *zVfs, const char *zName, int nOut, char *zOut);
static int feosRandomness(sqlite3_vfs *zVfs, int nByte, char *zOut);
static void *feosDlOpen(sqlite3_vfs *pVfs, const char *zPath);
static void feosDlError(sqlite3_vfs *pVfs, int nByte, char *zErrMsg);
static void (*feosDlSym(sqlite3_vfs *pVfs, void *pH, const char *z))(void);
static void feosDlClose(sqlite3_vfs *pVfs, void *pHandle);
static int feosSleep(sqlite3_vfs *pVfs, int nMicro);
static int feosCurrentTimeInt64(sqlite3_vfs *pVfs, sqlite3_int64 *pTime);

static sqlite3_vfs FeOSvfs = {
  .iVersion          = 2,
  .szOsFile          = sizeof(feosFile),
  .mxPathname        = 1024,
  .pNext             = 0,
  .zName             = "feos",
  .pAppData          = 0,
  .xOpen             = feosOpen,
  .xDelete           = feosDelete,
  .xAccess           = feosAccess,
  .xFullPathname     = feosFullPathname,
  .xDlOpen           = feosDlOpen,
  .xDlError          = feosDlError,
  .xDlSym            = feosDlSym,
  .xDlClose          = feosDlClose,
  .xRandomness       = feosRandomness,
  .xSleep            = feosSleep,
  .xCurrentTime      = 0,
  .xGetLastError     = 0,
  .xCurrentTimeInt64 = feosCurrentTimeInt64,
};

int sqlite3_os_init(void) {
  BEGIN;
  sqlite3_vfs_register(&FeOSvfs, 1);
  
  RETURN(SQLITE_OK);
}

int sqlite3_os_end(void) {
  BEGIN;
  RETURN(SQLITE_OK);
}

static int feosClose(sqlite3_file *pFile) {
  BEGIN;
  feosFile *p = (feosFile*)pFile;
  int rc = fclose(p->fp);

  if(rc != 0)
    RETURN(SQLITE_IOERR_CLOSE);
  else
    RETURN(SQLITE_OK);
}

static int feosRead(sqlite3_file *pFile, void *zBuf, int iAmt, sqlite_int64 iOfst) {
  BEGIN;
  int rc;
  feosFile *p = (feosFile*)pFile;

  if(fseek(p->fp, iOfst, SEEK_SET) != 0)
    RETURN(SQLITE_IOERR_READ);

  rc = fread(zBuf, iAmt, 1, p->fp);
  if(rc < 0)
    RETURN(SQLITE_IOERR_READ);
  if(rc == 0) {
    memset(zBuf, 0, iAmt);
    RETURN(SQLITE_IOERR_SHORT_READ);
  }

  RETURN(SQLITE_OK);
}

static int feosWrite(sqlite3_file *pFile, const void *zBuf, int iAmt, sqlite_int64 iOfst) {
  BEGIN;
  feosFile *p = (feosFile*)pFile;

  if(fseek(p->fp, iOfst, SEEK_SET) != 0)
    RETURN(SQLITE_IOERR_WRITE);

  if(fwrite(zBuf, iAmt, 1, p->fp) != 1)
    RETURN(SQLITE_IOERR_WRITE);
  RETURN(SQLITE_OK);
}

static int feosTruncate(sqlite3_file *pFile, sqlite_int64 size) {
  BEGIN;
  RETURN(SQLITE_OK);
}

static int feosSync(sqlite3_file *pFile, int flags) {
  BEGIN;
  feosFile *p = (feosFile*)pFile;

  if(fflush(p->fp) != 0)
    RETURN(SQLITE_IOERR_FSYNC);
  RETURN(SQLITE_OK);
}

static int feosFileSize(sqlite3_file *pFile, sqlite_int64 *pSize) {
  BEGIN;
  int rc;
  feosFile *p = (feosFile*)pFile;

  if(fseek(p->fp, 0, SEEK_END) != 0)
    RETURN(SQLITE_IOERR_FSTAT);

  rc = ftell(p->fp);
  if(rc == -1)
    RETURN(SQLITE_IOERR_FSTAT);

  *pSize = rc;
  RETURN(SQLITE_OK);
}

static int feosLock(sqlite3_file *pFile, int eLock) {
  BEGIN;
  RETURN(SQLITE_OK);
}

static int feosUnlock(sqlite3_file *pFile, int eLock) {
  BEGIN;
  RETURN(SQLITE_OK);
}

static int feosCheckReservedLock(sqlite3_file *pFile, int *pResOut) {
  BEGIN;
  *pResOut = 0;
  RETURN(SQLITE_OK);
}

static int feosFileControl(sqlite3_file *pFile, int op, void *pArg) {
  BEGIN;
  RETURN(SQLITE_OK);
}

static int feosSectorSize(sqlite3_file *pFile) {
  BEGIN;
  RETURN(512);
}

static int feosDeviceCharacteristics(sqlite3_file *pFile) {
  BEGIN;
  RETURN(0);
}

static int feosOpen(sqlite3_vfs *pVfs, const char *zName, sqlite3_file *pFile, int flags, int *pOutFlags) {
  BEGIN;
  feosFile *p = (feosFile*)pFile;

  if(zName == NULL) {
    RETURN(SQLITE_IOERR);
  }

  memset(p, 0, sizeof(feosFile));

  if(flags & SQLITE_OPEN_READONLY) {
    p->fp = fopen(zName, "rb");
  }
  else {
    p->fp = fopen(zName, "r+b");
  }

  if(p->fp == NULL && errno == ENOENT && (flags&SQLITE_OPEN_CREATE)) {
    p->fp = fopen(zName, "w+b");
  }
  if(p->fp == NULL) {
    RETURN(SQLITE_CANTOPEN);
  }

  p->base.pMethods = &feosIO;
  RETURN(SQLITE_OK);
}

static int feosDelete(sqlite3_vfs *zVfs, const char *zName, int syncDir) {
  BEGIN;
  remove(zName);
  RETURN(SQLITE_OK);
}

static int feosAccess(sqlite3_vfs *zVfs, const char *zName, int flags, int *pResOut) {
  BEGIN;
  struct stat statbuf;
  *pResOut = stat(zName, &statbuf) == 0;

  RETURN(SQLITE_OK);
}

static int feosFullPathname(sqlite3_vfs *zVfs, const char *zName, int nOut, char *zOut) {
  BEGIN;
  zOut[nOut-1] = '\0';

  if(strncmp(zName, "fat:/", sizeof("fat:/")) == 0
  || strncmp(zName, "sd:/",  sizeof("sd:/"))  == 0
  || zName[0] == '/') {
    sqlite3_snprintf(nOut, zOut, "%s", zName);
  }
  else {
    int nCwd; 
    if(getcwd(zOut, nOut-1) == NULL)
      RETURN(SQLITE_IOERR);
    nCwd = (int)strlen(zOut);
    sqlite3_snprintf(nOut-nCwd, &zOut[nCwd], "/%s", zName);
  }

  RETURN(SQLITE_OK);
}

static int feosRandomness(sqlite3_vfs *zVfs, int nByte, char *zOut) {
  BEGIN;
  RETURN(SQLITE_OK);
}

static void *feosDlOpen(sqlite3_vfs *pVfs, const char *zPath) {
  BEGIN;
  RETURN(0);
}

static void feosDlError(sqlite3_vfs *pVfs, int nByte, char *zErrMsg) {
  BEGIN;
  sqlite3_snprintf(nByte, zErrMsg, "Loadable extensions are not supported");
  zErrMsg[nByte-1] = 0;
  return;
}

static void (*feosDlSym(sqlite3_vfs *pVfs, void *pH, const char *z))(void) {
  BEGIN;
  RETURN(0);
}

static void feosDlClose(sqlite3_vfs *pVfs, void *pHandle) {
  BEGIN;
  return;
}

static int feosSleep(sqlite3_vfs *pVfs, int nMicro) {
  BEGIN;
  RETURN(0);
}

static int feosCurrentTimeInt64(sqlite3_vfs *pVfs, sqlite3_int64 *pTime) {
  BEGIN;
  static const sqlite3_int64 unixEpoch = 24405875*(sqlite3_int64)8640000;
  time_t t;
  time(&t);
  *pTime = ((sqlite3_int64)t)*1000 + unixEpoch;
 
  RETURN(SQLITE_OK);
}

