/*
** SQLite Logging VFS Extension
** 
** This VFS wraps the default VFS and logs all file operations.
** Useful for debugging, monitoring, or audit trails.
*/

#include "sqlite3.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <stdarg.h>
#include <dirent.h>
#include <unistd.h>
#include "block.h"

/*
** Forward declarations
*/
static sqlite3_vfs *pDefaultVfs = 0;
static FILE *logFile = 0;
static int useBlockStorage = 0; /* 0 = use default VFS, 1 = use block storage */
static int loggingEnabled = 1; /* 0 = disable logging, 1 = enable logging */

/*
** Logging helper function
*/
static void logVfsOperation(const char *operation, const char *filename, const char *format, ...) {
    if (!logFile || !loggingEnabled) return;
    
    time_t now;
    time(&now);
    char *timeStr = ctime(&now);
    timeStr[strlen(timeStr)-1] = '\0'; // Remove newline
    
    fprintf(logFile, "[%s] %s: %s - ", timeStr, operation, filename ? filename : "NULL");
    
    va_list args;
    va_start(args, format);
    vfprintf(logFile, format, args);
    va_end(args);
    
    fprintf(logFile, "\n");
    fflush(logFile);
}

/*
** File structure for our VFS
*/
typedef struct LoggingFile LoggingFile;
struct LoggingFile {
    sqlite3_file base;          /* Base class. Must be first. */
    sqlite3_file *pReal;        /* The real underlying file */
    block_file_t *pBlock;       /* Block-based file handle */
    char *zName;               /* Name of the file */
};

/*
** Close a file.
*/
static int loggingClose(sqlite3_file *pFile){
    LoggingFile *p = (LoggingFile*)pFile;
    int rc = SQLITE_OK;
    
    logVfsOperation("CLOSE", p->zName, "Closing file");
    
    if (useBlockStorage && p->pBlock) {
        rc = block_close(p->pBlock);
        if (rc != 0) rc = SQLITE_IOERR_CLOSE;
    } else if (p->pReal) {
        rc = p->pReal->pMethods->xClose(p->pReal);
        sqlite3_free(p->pReal);
    }
    
    sqlite3_free(p->zName);
    
    logVfsOperation("CLOSE", p->zName, "File closed, rc=%d", rc);
    return rc;
}

/*
** Read data from a file.
*/
static int loggingRead(sqlite3_file *pFile, void *zBuf, int iAmt, sqlite3_int64 iOfst){
    LoggingFile *p = (LoggingFile*)pFile;
    int rc;
    
    logVfsOperation("READ", p->zName, "Reading %d bytes at offset %lld", iAmt, iOfst);
    
    if (useBlockStorage && p->pBlock) {
        int bytes_read = block_read(p->pBlock, zBuf, iAmt, iOfst);
        if (bytes_read < 0) {
            /* Actual I/O error */
            rc = SQLITE_IOERR_READ;
        } else {
            /* 
             * block_read() handles zero-filling internally for missing data.
             * For SQLite VFS, reading beyond EOF or from new files should
             * return SQLITE_OK with zero-filled data, not SHORT_READ.
             */
            rc = SQLITE_OK;
        }
    } else {
        rc = p->pReal->pMethods->xRead(p->pReal, zBuf, iAmt, iOfst);
    }
    
    logVfsOperation("READ", p->zName, "Read completed, rc=%d", rc);
    return rc;
}

/*
** Write data to a file.
*/
static int loggingWrite(sqlite3_file *pFile, const void *zBuf, int iAmt, sqlite3_int64 iOfst){
    LoggingFile *p = (LoggingFile*)pFile;
    int rc;
    
    logVfsOperation("WRITE", p->zName, "Writing %d bytes at offset %lld", iAmt, iOfst);
    
    if (useBlockStorage && p->pBlock) {
        int bytes_written = block_write(p->pBlock, zBuf, iAmt, iOfst);
        if (bytes_written == iAmt) {
            rc = SQLITE_OK;
        } else {
            rc = SQLITE_IOERR_WRITE;
        }
    } else {
        rc = p->pReal->pMethods->xWrite(p->pReal, zBuf, iAmt, iOfst);
    }
    
    logVfsOperation("WRITE", p->zName, "Write completed, rc=%d", rc);
    return rc;
}

/*
** Truncate a file.
*/
static int loggingTruncate(sqlite3_file *pFile, sqlite3_int64 size){
    LoggingFile *p = (LoggingFile*)pFile;
    int rc;
    
    logVfsOperation("TRUNCATE", p->zName, "Truncating to %lld bytes", size);
    
    if (useBlockStorage && p->pBlock) {
        rc = block_truncate(p->pBlock, size);
        if (rc != 0) rc = SQLITE_IOERR_TRUNCATE;
    } else {
        rc = p->pReal->pMethods->xTruncate(p->pReal, size);
    }
    
    logVfsOperation("TRUNCATE", p->zName, "Truncate completed, rc=%d", rc);
    return rc;
}

/*
** Sync a file.
*/
static int loggingSync(sqlite3_file *pFile, int flags){
    LoggingFile *p = (LoggingFile*)pFile;
    int rc;
    
    logVfsOperation("SYNC", p->zName, "Syncing with flags %d", flags);
    
    if (useBlockStorage && p->pBlock) {
        /* Block storage doesn't need explicit sync - data is written immediately */
        rc = SQLITE_OK;
    } else {
        rc = p->pReal->pMethods->xSync(p->pReal, flags);
    }
    
    logVfsOperation("SYNC", p->zName, "Sync completed, rc=%d", rc);
    return rc;
}

/*
** Return the current file-size of a file.
*/
static int loggingFileSize(sqlite3_file *pFile, sqlite3_int64 *pSize){
    LoggingFile *p = (LoggingFile*)pFile;
    int rc;
    
    if (useBlockStorage && p->pBlock) {
        long long size = block_file_size(p->pBlock);
        if (size >= 0) {
            *pSize = size;
            rc = SQLITE_OK;
        } else {
            *pSize = 0;
            rc = SQLITE_IOERR_FSTAT;
        }
    } else {
        rc = p->pReal->pMethods->xFileSize(p->pReal, pSize);
    }
    
    logVfsOperation("FILESIZE", p->zName, "File size: %lld bytes, rc=%d", *pSize, rc);
    return rc;
}

/*
** Lock a file.
*/
static int loggingLock(sqlite3_file *pFile, int eLock){
    LoggingFile *p = (LoggingFile*)pFile;
    int rc;
    
    const char *lockType = "UNKNOWN";
    switch(eLock){
        case SQLITE_LOCK_NONE:      lockType = "NONE"; break;
        case SQLITE_LOCK_SHARED:    lockType = "SHARED"; break;
        case SQLITE_LOCK_RESERVED:  lockType = "RESERVED"; break;
        case SQLITE_LOCK_PENDING:   lockType = "PENDING"; break;
        case SQLITE_LOCK_EXCLUSIVE: lockType = "EXCLUSIVE"; break;
    }
    
    logVfsOperation("LOCK", p->zName, "Acquiring %s lock", lockType);
    
    if (useBlockStorage && p->pBlock) {
        /* Block storage doesn't need file locks - always succeed */
        rc = SQLITE_OK;
    } else {
        rc = p->pReal->pMethods->xLock(p->pReal, eLock);
    }
    
    logVfsOperation("LOCK", p->zName, "Lock acquisition completed, rc=%d", rc);
    return rc;
}

/*
** Unlock a file.
*/
static int loggingUnlock(sqlite3_file *pFile, int eLock){
    LoggingFile *p = (LoggingFile*)pFile;
    int rc;
    
    const char *lockType = "UNKNOWN";
    switch(eLock){
        case SQLITE_LOCK_NONE:      lockType = "NONE"; break;
        case SQLITE_LOCK_SHARED:    lockType = "SHARED"; break;
        case SQLITE_LOCK_RESERVED:  lockType = "RESERVED"; break;
        case SQLITE_LOCK_PENDING:   lockType = "PENDING"; break;
        case SQLITE_LOCK_EXCLUSIVE: lockType = "EXCLUSIVE"; break;
    }
    
    logVfsOperation("UNLOCK", p->zName, "Releasing to %s lock", lockType);
    
    if (useBlockStorage && p->pBlock) {
        /* Block storage doesn't need file locks - always succeed */
        rc = SQLITE_OK;
    } else {
        rc = p->pReal->pMethods->xUnlock(p->pReal, eLock);
    }
    
    logVfsOperation("UNLOCK", p->zName, "Lock release completed, rc=%d", rc);
    return rc;
}

/*
** Check if another file-handle holds a RESERVED lock on a file.
*/
static int loggingCheckReservedLock(sqlite3_file *pFile, int *pResOut){
    LoggingFile *p = (LoggingFile*)pFile;
    int rc;
    
    if (useBlockStorage && p->pBlock) {
        /* Block storage doesn't use file locks - no reserved lock */
        *pResOut = 0;
        rc = SQLITE_OK;
    } else {
        rc = p->pReal->pMethods->xCheckReservedLock(p->pReal, pResOut);
    }
    
    logVfsOperation("CHECK_RESERVED", p->zName, "Reserved lock check: %s, rc=%d", 
                   *pResOut ? "RESERVED" : "NOT RESERVED", rc);
    return rc;
}

/*
** File control method.
*/
static int loggingFileControl(sqlite3_file *pFile, int op, void *pArg){
    LoggingFile *p = (LoggingFile*)pFile;
    int rc;
    
    logVfsOperation("FILE_CONTROL", p->zName, "File control operation %d", op);
    
    if (useBlockStorage && p->pBlock) {
        /* Block storage doesn't support file control operations */
        rc = SQLITE_NOTFOUND;
    } else {
        rc = p->pReal->pMethods->xFileControl(p->pReal, op, pArg);
    }
    
    logVfsOperation("FILE_CONTROL", p->zName, "File control completed, rc=%d", rc);
    return rc;
}

/*
** Return the sector-size in bytes for a file.
*/
static int loggingSectorSize(sqlite3_file *pFile){
    LoggingFile *p = (LoggingFile*)pFile;
    int sectorSize;
    
    if (useBlockStorage && p->pBlock) {
        /* Block storage uses 4KB sectors */
        sectorSize = 4096;
    } else {
        sectorSize = p->pReal->pMethods->xSectorSize(p->pReal);
    }
    
    logVfsOperation("SECTOR_SIZE", p->zName, "Sector size: %d bytes", sectorSize);
    return sectorSize;
}

/*
** Return the device characteristic flags supported by a file.
*/
static int loggingDeviceCharacteristics(sqlite3_file *pFile){
    LoggingFile *p = (LoggingFile*)pFile;
    int characteristics;
    
    if (useBlockStorage && p->pBlock) {
        /* Block storage characteristics */
        characteristics = SQLITE_IOCAP_ATOMIC4K | SQLITE_IOCAP_SAFE_APPEND;
    } else {
        characteristics = p->pReal->pMethods->xDeviceCharacteristics(p->pReal);
    }
    
    logVfsOperation("DEVICE_CHARS", p->zName, "Device characteristics: 0x%x", characteristics);
    return characteristics;
}

/*
** Methods for LoggingFile
*/
static const sqlite3_io_methods loggingIoMethods = {
    3,                              /* iVersion */
    loggingClose,                   /* xClose */
    loggingRead,                    /* xRead */
    loggingWrite,                   /* xWrite */
    loggingTruncate,                /* xTruncate */
    loggingSync,                    /* xSync */
    loggingFileSize,                /* xFileSize */
    loggingLock,                    /* xLock */
    loggingUnlock,                  /* xUnlock */
    loggingCheckReservedLock,       /* xCheckReservedLock */
    loggingFileControl,             /* xFileControl */
    loggingSectorSize,              /* xSectorSize */
    loggingDeviceCharacteristics,   /* xDeviceCharacteristics */
    0,                              /* xShmMap */
    0,                              /* xShmLock */
    0,                              /* xShmBarrier */
    0,                              /* xShmUnmap */
    0,                              /* xFetch */
    0                               /* xUnfetch */
};

/*
** Open a file.
*/
static int loggingOpen(
    sqlite3_vfs *pVfs,           /* The VFS */
    const char *zName,           /* File to open, or 0 for a temp file */
    sqlite3_file *pFile,         /* Pointer to LoggingFile struct to populate */
    int flags,                   /* Input SQLITE_OPEN_XXX flags */
    int *pOutFlags               /* Output SQLITE_OPEN_XXX flags (or NULL) */
){
    LoggingFile *p = (LoggingFile*)pFile;
    int rc;
    
    logVfsOperation("OPEN", zName, "Opening file with flags 0x%x", flags);
    
    /* Initialize the struct */
    p->pReal = 0;
    p->pBlock = 0;
    
    if (useBlockStorage) {
        /* Use block storage */
        char *temp_filename = NULL;
        const char *filename = zName;
        if (!filename) {
            temp_filename = sqlite3_mprintf("temp_file_%p", (void*)p);
            filename = temp_filename;
        }
        
        rc = block_open(filename, &p->pBlock);
        
        if (temp_filename) {
            sqlite3_free(temp_filename);
        }
        
        if (rc != 0) {
            logVfsOperation("OPEN", zName, "Failed to open block file, rc=%d", rc);
            return SQLITE_CANTOPEN;
        }
        
        if (pOutFlags) {
            *pOutFlags = flags;
        }
    } else {
        /* Use default VFS */
        p->pReal = (sqlite3_file*)sqlite3_malloc(pDefaultVfs->szOsFile);
        if( p->pReal==0 ){
            logVfsOperation("OPEN", zName, "Failed to allocate memory for real file");
            return SQLITE_NOMEM;
        }
        
        rc = pDefaultVfs->xOpen(pDefaultVfs, zName, p->pReal, flags, pOutFlags);
        if( rc!=SQLITE_OK ){
            sqlite3_free(p->pReal);
            logVfsOperation("OPEN", zName, "Failed to open real file, rc=%d", rc);
            return rc;
        }
    }
    
    if( zName ){
        p->zName = sqlite3_mprintf("%s", zName);
    } else {
        p->zName = sqlite3_mprintf("temp_file_%p", (void*)p);
    }
    
    p->base.pMethods = &loggingIoMethods;
    
    logVfsOperation("OPEN", p->zName, "File opened successfully (%s)", 
                   useBlockStorage ? "block storage" : "default VFS");
    return SQLITE_OK;
}

/*
** Delete a file.
*/

static int remove_directory_recursive(const char *path) {
    DIR *d = opendir(path);
    if (!d) return 0; /* Directory doesn't exist, consider success */
    
    struct dirent *entry;
    int result = 0;
    
    while ((entry = readdir(d)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        
        char full_path[1024];
        snprintf(full_path, sizeof(full_path), "%s/%s", path, entry->d_name);
        
        if (unlink(full_path) != 0) {
            result = -1;
        }
    }
    
    closedir(d);
    
    if (rmdir(path) != 0) {
        result = -1;
    }
    
    return result;
}

static int loggingDelete(sqlite3_vfs *pVfs, const char *zPath, int syncDir){
    int rc;
    
    logVfsOperation("DELETE", zPath, "Deleting file, syncDir=%d", syncDir);
    
    if (useBlockStorage) {
        /* For block storage, delete the block directory */
        char block_dir[1024];
        snprintf(block_dir, sizeof(block_dir), "%s.blocks", zPath);
        
        /* Remove the block directory and its contents using POSIX calls */
        int dir_rc = remove_directory_recursive(block_dir);
        
        /* For block storage, also try to delete regular file if it exists */
        pDefaultVfs->xDelete(pDefaultVfs, zPath, syncDir);
        
        rc = (dir_rc == 0) ? SQLITE_OK : SQLITE_IOERR_DELETE;
    } else {
        rc = pDefaultVfs->xDelete(pDefaultVfs, zPath, syncDir);
    }
    
    logVfsOperation("DELETE", zPath, "Delete completed, rc=%d", rc);
    return rc;
}

/*
** Test for access permissions.
*/
static int loggingAccess(sqlite3_vfs *pVfs, const char *zPath, int flags, int *pResOut){
    int rc;
    
    const char *accessType = "UNKNOWN";
    switch(flags){
        case SQLITE_ACCESS_EXISTS:    accessType = "EXISTS"; break;
        case SQLITE_ACCESS_READWRITE: accessType = "READWRITE"; break;
        case SQLITE_ACCESS_READ:      accessType = "READ"; break;
    }
    
    logVfsOperation("ACCESS", zPath, "Checking %s access", accessType);
    
    rc = pDefaultVfs->xAccess(pDefaultVfs, zPath, flags, pResOut);
    
    logVfsOperation("ACCESS", zPath, "Access check result: %s, rc=%d", 
                   *pResOut ? "GRANTED" : "DENIED", rc);
    return rc;
}

/*
** Populate buffer zOut with the full canonical pathname corresponding
** to the pathname in zPath.
*/
static int loggingFullPathname(sqlite3_vfs *pVfs, const char *zPath, int nOut, char *zOut){
    int rc;
    
    logVfsOperation("FULLPATH", zPath, "Getting full pathname");
    
    rc = pDefaultVfs->xFullPathname(pDefaultVfs, zPath, nOut, zOut);
    
    logVfsOperation("FULLPATH", zPath, "Full path: %s, rc=%d", zOut, rc);
    return rc;
}

/*
** The remaining VFS methods are pass-throughs to the default VFS.
*/
static void *loggingDlOpen(sqlite3_vfs *pVfs, const char *zPath){
    return pDefaultVfs->xDlOpen(pDefaultVfs, zPath);
}
static void loggingDlError(sqlite3_vfs *pVfs, int nByte, char *zErrMsg){
    pDefaultVfs->xDlError(pDefaultVfs, nByte, zErrMsg);
}
static void (*loggingDlSym(sqlite3_vfs *pVfs, void *p, const char *zSym))(void){
    return pDefaultVfs->xDlSym(pDefaultVfs, p, zSym);
}
static void loggingDlClose(sqlite3_vfs *pVfs, void *pHandle){
    pDefaultVfs->xDlClose(pDefaultVfs, pHandle);
}
static int loggingRandomness(sqlite3_vfs *pVfs, int nByte, char *zByte){
    return pDefaultVfs->xRandomness(pDefaultVfs, nByte, zByte);
}
static int loggingSleep(sqlite3_vfs *pVfs, int nMicro){
    return pDefaultVfs->xSleep(pDefaultVfs, nMicro);
}
static int loggingCurrentTime(sqlite3_vfs *pVfs, double *pTimeOut){
    return pDefaultVfs->xCurrentTime(pDefaultVfs, pTimeOut);
}
static int loggingGetLastError(sqlite3_vfs *pVfs, int nByte, char *zErrMsg){
    return pDefaultVfs->xGetLastError(pDefaultVfs, nByte, zErrMsg);
}
static int loggingCurrentTimeInt64(sqlite3_vfs *pVfs, sqlite3_int64 *pTimeOut){
    return pDefaultVfs->xCurrentTimeInt64(pDefaultVfs, pTimeOut);
}

/*
** The VFS structure for the logging VFS.
*/
static sqlite3_vfs loggingVfs = {
    3,                      /* iVersion */
    0,                      /* szOsFile (set when registered) */
    1024,                   /* mxPathname */
    0,                      /* pNext */
    "logging",              /* zName */
    0,                      /* pAppData */
    loggingOpen,            /* xOpen */
    loggingDelete,          /* xDelete */
    loggingAccess,          /* xAccess */
    loggingFullPathname,    /* xFullPathname */
    loggingDlOpen,          /* xDlOpen */
    loggingDlError,         /* xDlError */
    loggingDlSym,           /* xDlSym */
    loggingDlClose,         /* xDlClose */
    loggingRandomness,      /* xRandomness */
    loggingSleep,           /* xSleep */
    loggingCurrentTime,     /* xCurrentTime */
    loggingGetLastError,    /* xGetLastError */
    loggingCurrentTimeInt64 /* xCurrentTimeInt64 */
};

/*
** Enable or disable block storage.
*/
void sqlite3_loggingvfs_set_block_storage(int enable){
    useBlockStorage = enable;
    logVfsOperation("CONFIG", NULL, "Block storage %s", enable ? "ENABLED" : "DISABLED");
}

/*
** Enable or disable logging.
*/
void sqlite3_loggingvfs_set_logging(int enable){
    loggingEnabled = enable;
}

/*
** Register the logging VFS.
*/
int sqlite3_loggingvfs_init(const char *logFilePath){
    pDefaultVfs = sqlite3_vfs_find(0);
    if( pDefaultVfs==0 ) return SQLITE_ERROR;
    
    // Only open log file if logging is enabled
    if( loggingEnabled ){
        if( logFilePath ){
            logFile = fopen(logFilePath, "a");
            if( !logFile ){
                fprintf(stderr, "Failed to open log file: %s\n", logFilePath);
                return SQLITE_ERROR;
            }
        } else {
            logFile = stdout;  // Default to stdout
        }
    }
    
    loggingVfs.szOsFile = sizeof(LoggingFile);
    
    logVfsOperation("INIT", NULL, "Logging VFS initialized with log file: %s, block storage: %s", 
                   loggingEnabled ? (logFilePath ? logFilePath : "stdout") : "DISABLED", useBlockStorage ? "ENABLED" : "DISABLED");
    
    return sqlite3_vfs_register(&loggingVfs, 0);
}

/*
** Unregister the logging VFS and close log file.
*/
int sqlite3_loggingvfs_shutdown(){
    int rc = sqlite3_vfs_unregister(&loggingVfs);
    
    if( logFile && logFile != stdout ){
        fclose(logFile);
        logFile = 0;
    }
    
    return rc;
}

/*
** Usage example as a C library:
**
** #include "sqlite3.h"
** 
** int main(){
**     sqlite3 *db;
**     int rc;
**     
**     // Initialize the logging VFS
**     sqlite3_loggingvfs_init("sqlite_operations.log");
**     
**     // Open database using the logging VFS
**     rc = sqlite3_open_v2("test.db", &db, 
**                          SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE,
**                          "logging");
**     
**     if( rc ){
**         fprintf(stderr, "Can't open database: %s\n", sqlite3_errmsg(db));
**         return 1;
**     }
**     
**     // Perform database operations - all will be logged
**     sqlite3_exec(db, "CREATE TABLE test(id INTEGER, name TEXT);", 0, 0, 0);
**     sqlite3_exec(db, "INSERT INTO test VALUES(1, 'Hello');", 0, 0, 0);
**     
**     sqlite3_close(db);
**     sqlite3_loggingvfs_shutdown();
**     
**     return 0;
** }
*/
