#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>
#include <sys/stat.h>
#include "sqlite3.h"

// Forward declarations from your VFS
extern int sqlite3_loggingvfs_init(const char *logFilePath);
extern int sqlite3_loggingvfs_shutdown(void);
extern void sqlite3_loggingvfs_set_block_storage(int enable);

#define TEST_DB "simple_test.db"
#define TEST_LOG "simple_test.log"

// Cleanup function - call BEFORE each test
void cleanup_test_data() {
    printf("  Cleaning up...\n");
    
    // Remove all test files
    unlink(TEST_DB);
    unlink(TEST_DB "-journal");
    unlink(TEST_DB "-wal");
    unlink(TEST_DB "-shm");
    unlink(TEST_LOG);
    
    // Remove block directories
    system("rm -rf " TEST_DB ".blocks");
    system("rm -rf " TEST_DB "-journal.blocks");
    system("rm -rf " TEST_DB "-wal.blocks");
    system("rm -rf " TEST_DB "-shm.blocks");
    
    system("rm -rf simple_test.db*");
    system("rm -rf debug_test.db*");
}

// Test 1: Basic block storage
void test_block_storage_basic() {
    printf("Test 1: Basic block storage\n");
    cleanup_test_data();
    
    sqlite3 *db;
    int rc;
    
    rc = sqlite3_loggingvfs_init(TEST_LOG);
    assert(rc == SQLITE_OK);
    
    sqlite3_loggingvfs_set_block_storage(1);
    
    rc = sqlite3_open_v2(TEST_DB, &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, "logging");
    assert(rc == SQLITE_OK);
    
    // Force WAL mode off (use rollback journal)
    char *errmsg = NULL;
    rc = sqlite3_exec(db, "PRAGMA journal_mode=DELETE", NULL, NULL, &errmsg);
    if (rc != SQLITE_OK) {
        printf("  WARNING: Could not set journal mode: %s\n", errmsg ? errmsg : "Unknown");
        if (errmsg) sqlite3_free(errmsg);
    }
    
    rc = sqlite3_exec(db, "CREATE TABLE simple(id INTEGER, value TEXT)", NULL, NULL, &errmsg);
    if (rc != SQLITE_OK) {
        printf("  ERROR: Create table failed: %s\n", errmsg ? errmsg : "Unknown");
        if (errmsg) sqlite3_free(errmsg);
        sqlite3_close(db);
        sqlite3_loggingvfs_shutdown();
        exit(1);
    }
    
    rc = sqlite3_exec(db, "INSERT INTO simple VALUES(1, 'test')", NULL, NULL, &errmsg);
    assert(rc == SQLITE_OK);
    
    sqlite3_close(db);
    sqlite3_loggingvfs_shutdown();
    
    // Verify block directory was created
    struct stat st;
    assert(stat(TEST_DB ".blocks", &st) == 0);
    
    printf("  PASSED\n\n");
}

// Test 2: Regular VFS mode
void test_regular_vfs() {
    printf("Test 2: Regular VFS mode\n");
    cleanup_test_data();
    
    sqlite3 *db;
    int rc;
    
    rc = sqlite3_loggingvfs_init(TEST_LOG);
    assert(rc == SQLITE_OK);
    
    sqlite3_loggingvfs_set_block_storage(0);  // Disable block storage
    
    rc = sqlite3_open_v2(TEST_DB, &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, "logging");
    assert(rc == SQLITE_OK);
    
    char *errmsg = NULL;
    rc = sqlite3_exec(db, "CREATE TABLE regular(id INTEGER)", NULL, NULL, &errmsg);
    if (rc != SQLITE_OK) {
        printf("  ERROR: Create table failed: %s\n", errmsg ? errmsg : "Unknown");
        if (errmsg) sqlite3_free(errmsg);
        exit(1);
    }
    
    sqlite3_close(db);
    sqlite3_loggingvfs_shutdown();
    
    // Verify regular file was created, not block directory
    struct stat st;
    assert(stat(TEST_DB, &st) == 0);
    assert(S_ISREG(st.st_mode));
    
    // Verify block directory was NOT created
    assert(stat(TEST_DB ".blocks", &st) != 0);
    
    printf("  PASSED\n\n");
}

// Test 3: Data persistence 
void test_persistence() {
    printf("Test 3: Data persistence\n");
    cleanup_test_data();
    
    sqlite3 *db;
    int rc;
    
    // Write data
    rc = sqlite3_loggingvfs_init(TEST_LOG);
    assert(rc == SQLITE_OK);
    sqlite3_loggingvfs_set_block_storage(1);
    
    rc = sqlite3_open_v2(TEST_DB, &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, "logging");
    assert(rc == SQLITE_OK);
    
    char *errmsg = NULL;
    rc = sqlite3_exec(db, "PRAGMA journal_mode=DELETE", NULL, NULL, &errmsg);
    rc = sqlite3_exec(db, "CREATE TABLE persist(data TEXT)", NULL, NULL, &errmsg);
    assert(rc == SQLITE_OK);
    
    rc = sqlite3_exec(db, "INSERT INTO persist VALUES('persistent_data')", NULL, NULL, &errmsg);
    assert(rc == SQLITE_OK);
    
    sqlite3_close(db);
    sqlite3_loggingvfs_shutdown();
    
    // Read data back
    rc = sqlite3_loggingvfs_init(TEST_LOG);
    assert(rc == SQLITE_OK);
    sqlite3_loggingvfs_set_block_storage(1);
    
    rc = sqlite3_open_v2(TEST_DB, &db, SQLITE_OPEN_READONLY, "logging");
    assert(rc == SQLITE_OK);
    
    sqlite3_stmt *stmt;
    rc = sqlite3_prepare_v2(db, "SELECT data FROM persist", -1, &stmt, NULL);
    assert(rc == SQLITE_OK);
    
    rc = sqlite3_step(stmt);
    assert(rc == SQLITE_ROW);
    
    const char *data = (const char*)sqlite3_column_text(stmt, 0);
    assert(strcmp(data, "persistent_data") == 0);
    
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    sqlite3_loggingvfs_shutdown();
    
    printf("  PASSED\n\n");
}

int main() {
    printf("Running simple VFS tests...\n\n");
    
    test_block_storage_basic();
    test_regular_vfs();
    test_persistence();
    
    // Final cleanup
    cleanup_test_data();
    
    printf("All simple tests PASSED! ✅\n");
    return 0;
}