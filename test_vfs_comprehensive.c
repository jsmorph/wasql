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

// Test database files
#define TEST_DB "test_comprehensive.db"
#define TEST_LOG "test_comprehensive.log"

// Comprehensive cleanup function - call BEFORE each test
void cleanup_all_test_data() {
    printf("  Cleaning up test data...\n");
    
    // Remove database files
    unlink(TEST_DB);
    unlink(TEST_DB "-journal");
    unlink(TEST_DB "-wal");
    unlink(TEST_DB "-shm");
    
    // Remove block directories and their contents
    system("rm -rf " TEST_DB ".blocks");
    system("rm -rf " TEST_DB "-journal.blocks");
    system("rm -rf " TEST_DB "-wal.blocks");
    system("rm -rf " TEST_DB "-shm.blocks");
    
    // Remove log file
    unlink(TEST_LOG);
    
    // Remove any other test artifacts
    system("rm -rf test_*.blocks");
    unlink("test.db");
    unlink("test.db-journal");
    system("rm -rf test.db.blocks");
    system("rm -rf test.db-journal.blocks");
    
    printf("  Cleanup complete.\n");
}

// Test 1: Basic block storage functionality
void test_basic_block_storage() {
    printf("Test 1: Basic block storage functionality\n");
    cleanup_all_test_data();
    
    sqlite3 *db;
    int rc;
    
    // Initialize VFS with block storage
    rc = sqlite3_loggingvfs_init(TEST_LOG);
    if (rc != SQLITE_OK) {
        printf("  ERROR: Failed to init VFS: %d\n", rc);
        exit(1);
    }
    
    sqlite3_loggingvfs_set_block_storage(1);
    
    // Open database
    rc = sqlite3_open_v2(TEST_DB, &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, "logging");
    if (rc != SQLITE_OK) {
        printf("  ERROR: Failed to open database: %s\n", sqlite3_errmsg(db));
        exit(1);
    }
    
    // Create table and insert data
    char *errmsg = NULL;
    rc = sqlite3_exec(db, "CREATE TABLE test(id INTEGER PRIMARY KEY, name TEXT)", NULL, NULL, &errmsg);
    if (rc != SQLITE_OK) {
        printf("  ERROR: Failed to create table: %s\n", errmsg ? errmsg : "Unknown error");
        if (errmsg) sqlite3_free(errmsg);
        exit(1);
    }
    
    rc = sqlite3_exec(db, "INSERT INTO test(name) VALUES('Alice'), ('Bob'), ('Charlie')", NULL, NULL, NULL);
    assert(rc == SQLITE_OK);
    
    // Query data
    sqlite3_stmt *stmt;
    rc = sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM test", -1, &stmt, NULL);
    assert(rc == SQLITE_OK);
    
    rc = sqlite3_step(stmt);
    assert(rc == SQLITE_ROW);
    
    int count = sqlite3_column_int(stmt, 0);
    assert(count == 3);
    
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    sqlite3_loggingvfs_shutdown();
    
    // Verify block files were created
    struct stat st;
    assert(stat(TEST_DB ".blocks", &st) == 0);
    assert(S_ISDIR(st.st_mode));
    
    printf("  PASSED\n\n");
}

// Test 2: Regular VFS mode (no block storage)
void test_regular_vfs_mode() {
    printf("Test 2: Regular VFS mode (no block storage)\n");
    cleanup_all_test_data();
    
    sqlite3 *db;
    int rc;
    
    // Initialize VFS without block storage
    rc = sqlite3_loggingvfs_init(TEST_LOG);
    assert(rc == SQLITE_OK);
    
    sqlite3_loggingvfs_set_block_storage(0);  // Explicitly disable
    
    // Open database
    rc = sqlite3_open_v2(TEST_DB, &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, "logging");
    assert(rc == SQLITE_OK);
    
    // Create table and insert data
    rc = sqlite3_exec(db, "CREATE TABLE test(id INTEGER PRIMARY KEY, name TEXT)", NULL, NULL, NULL);
    assert(rc == SQLITE_OK);
    
    rc = sqlite3_exec(db, "INSERT INTO test(name) VALUES('Test1'), ('Test2')", NULL, NULL, NULL);
    assert(rc == SQLITE_OK);
    
    sqlite3_close(db);
    sqlite3_loggingvfs_shutdown();
    
    // Verify regular file was created, not block files
    struct stat st;
    assert(stat(TEST_DB, &st) == 0);
    assert(S_ISREG(st.st_mode));
    
    // Verify block directory was NOT created
    assert(stat(TEST_DB ".blocks", &st) != 0);
    
    printf("  PASSED\n\n");
}

// Test 3: Data persistence across sessions
void test_data_persistence() {
    printf("Test 3: Data persistence across sessions\n");
    cleanup_all_test_data();
    
    sqlite3 *db;
    int rc;
    
    // Session 1: Write data
    rc = sqlite3_loggingvfs_init(TEST_LOG);
    assert(rc == SQLITE_OK);
    sqlite3_loggingvfs_set_block_storage(1);
    
    rc = sqlite3_open_v2(TEST_DB, &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, "logging");
    assert(rc == SQLITE_OK);
    
    rc = sqlite3_exec(db, "CREATE TABLE persist(id INTEGER, data TEXT)", NULL, NULL, NULL);
    assert(rc == SQLITE_OK);
    
    rc = sqlite3_exec(db, "INSERT INTO persist VALUES(1, 'persistent_data')", NULL, NULL, NULL);
    assert(rc == SQLITE_OK);
    
    sqlite3_close(db);
    sqlite3_loggingvfs_shutdown();
    
    // Session 2: Read data
    rc = sqlite3_loggingvfs_init(TEST_LOG);
    assert(rc == SQLITE_OK);
    sqlite3_loggingvfs_set_block_storage(1);
    
    rc = sqlite3_open_v2(TEST_DB, &db, SQLITE_OPEN_READONLY, "logging");
    assert(rc == SQLITE_OK);
    
    sqlite3_stmt *stmt;
    rc = sqlite3_prepare_v2(db, "SELECT data FROM persist WHERE id = 1", -1, &stmt, NULL);
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

// Test 4: Large data handling
void test_large_data() {
    printf("Test 4: Large data handling\n");
    cleanup_all_test_data();
    
    sqlite3 *db;
    int rc;
    
    rc = sqlite3_loggingvfs_init(TEST_LOG);
    assert(rc == SQLITE_OK);
    sqlite3_loggingvfs_set_block_storage(1);
    
    rc = sqlite3_open_v2(TEST_DB, &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, "logging");
    assert(rc == SQLITE_OK);
    
    // Create table for large data
    rc = sqlite3_exec(db, "CREATE TABLE large_data(id INTEGER, content BLOB)", NULL, NULL, NULL);
    assert(rc == SQLITE_OK);
    
    // Insert large blob (bigger than one block)
    char large_data[10000];
    for (int i = 0; i < 10000; i++) {
        large_data[i] = (char)(i % 256);
    }
    
    sqlite3_stmt *stmt;
    rc = sqlite3_prepare_v2(db, "INSERT INTO large_data(id, content) VALUES(1, ?)", -1, &stmt, NULL);
    assert(rc == SQLITE_OK);
    
    rc = sqlite3_bind_blob(stmt, 1, large_data, 10000, SQLITE_STATIC);
    assert(rc == SQLITE_OK);
    
    rc = sqlite3_step(stmt);
    assert(rc == SQLITE_DONE);
    
    sqlite3_finalize(stmt);
    
    // Read it back
    rc = sqlite3_prepare_v2(db, "SELECT content FROM large_data WHERE id = 1", -1, &stmt, NULL);
    assert(rc == SQLITE_OK);
    
    rc = sqlite3_step(stmt);
    assert(rc == SQLITE_ROW);
    
    const void *blob = sqlite3_column_blob(stmt, 0);
    int blob_size = sqlite3_column_bytes(stmt, 0);
    
    assert(blob_size == 10000);
    assert(memcmp(blob, large_data, 10000) == 0);
    
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    sqlite3_loggingvfs_shutdown();
    
    printf("  PASSED\n\n");
}

// Test 5: Multiple simultaneous connections
void test_multiple_connections() {
    printf("Test 5: Multiple simultaneous connections\n");
    cleanup_all_test_data();
    
    sqlite3 *db1, *db2;
    int rc;
    
    rc = sqlite3_loggingvfs_init(TEST_LOG);
    assert(rc == SQLITE_OK);
    sqlite3_loggingvfs_set_block_storage(1);
    
    // Open first connection
    rc = sqlite3_open_v2(TEST_DB, &db1, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, "logging");
    assert(rc == SQLITE_OK);
    
    // Create table
    rc = sqlite3_exec(db1, "CREATE TABLE multi(id INTEGER, value INTEGER)", NULL, NULL, NULL);
    assert(rc == SQLITE_OK);
    
    // Open second connection
    rc = sqlite3_open_v2(TEST_DB, &db2, SQLITE_OPEN_READWRITE, "logging");
    assert(rc == SQLITE_OK);
    
    // Insert from first connection
    rc = sqlite3_exec(db1, "INSERT INTO multi VALUES(1, 100)", NULL, NULL, NULL);
    assert(rc == SQLITE_OK);
    
    // Read from second connection
    sqlite3_stmt *stmt;
    rc = sqlite3_prepare_v2(db2, "SELECT value FROM multi WHERE id = 1", -1, &stmt, NULL);
    assert(rc == SQLITE_OK);
    
    rc = sqlite3_step(stmt);
    assert(rc == SQLITE_ROW);
    
    int value = sqlite3_column_int(stmt, 0);
    assert(value == 100);
    
    sqlite3_finalize(stmt);
    sqlite3_close(db1);
    sqlite3_close(db2);
    sqlite3_loggingvfs_shutdown();
    
    printf("  PASSED\n\n");
}

// Test 6: Switch between block and regular modes
void test_mode_switching() {
    printf("Test 6: Mode switching\n");
    cleanup_all_test_data();
    
    sqlite3 *db;
    int rc;
    
    // First: Use regular mode
    rc = sqlite3_loggingvfs_init(TEST_LOG);
    assert(rc == SQLITE_OK);
    sqlite3_loggingvfs_set_block_storage(0);
    
    rc = sqlite3_open_v2("regular_" TEST_DB, &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, "logging");
    assert(rc == SQLITE_OK);
    
    rc = sqlite3_exec(db, "CREATE TABLE mode_test(id INTEGER)", NULL, NULL, NULL);
    assert(rc == SQLITE_OK);
    
    sqlite3_close(db);
    sqlite3_loggingvfs_shutdown();
    
    // Second: Use block mode  
    rc = sqlite3_loggingvfs_init(TEST_LOG);
    assert(rc == SQLITE_OK);
    sqlite3_loggingvfs_set_block_storage(1);
    
    rc = sqlite3_open_v2("block_" TEST_DB, &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, "logging");
    assert(rc == SQLITE_OK);
    
    rc = sqlite3_exec(db, "CREATE TABLE mode_test(id INTEGER)", NULL, NULL, NULL);
    assert(rc == SQLITE_OK);
    
    sqlite3_close(db);
    sqlite3_loggingvfs_shutdown();
    
    // Verify both files exist in correct formats
    struct stat st;
    assert(stat("regular_" TEST_DB, &st) == 0);
    assert(S_ISREG(st.st_mode));
    
    assert(stat("block_" TEST_DB ".blocks", &st) == 0);
    assert(S_ISDIR(st.st_mode));
    
    // Clean up these specific files
    unlink("regular_" TEST_DB);
    system("rm -rf block_" TEST_DB ".blocks");
    
    printf("  PASSED\n\n");
}

// Test 7: Error handling
void test_error_handling() {
    printf("Test 7: Error handling\n");
    cleanup_all_test_data();
    
    sqlite3 *db;
    int rc;
    
    rc = sqlite3_loggingvfs_init(TEST_LOG);
    assert(rc == SQLITE_OK);
    sqlite3_loggingvfs_set_block_storage(1);
    
    // Try to open read-only database that doesn't exist
    rc = sqlite3_open_v2("nonexistent.db", &db, SQLITE_OPEN_READONLY, "logging");
    // Should fail gracefully
    if (rc == SQLITE_OK) {
        sqlite3_close(db);
    }
    
    // Create database and test invalid SQL
    rc = sqlite3_open_v2(TEST_DB, &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, "logging");
    assert(rc == SQLITE_OK);
    
    char *errmsg;
    rc = sqlite3_exec(db, "INVALID SQL STATEMENT", NULL, NULL, &errmsg);
    assert(rc != SQLITE_OK);
    if (errmsg) {
        sqlite3_free(errmsg);
    }
    
    sqlite3_close(db);
    sqlite3_loggingvfs_shutdown();
    
    printf("  PASSED\n\n");
}

int main() {
    printf("Running comprehensive VFS tests...\n\n");
    
    // Run all tests
    test_basic_block_storage();
    test_regular_vfs_mode(); 
    test_data_persistence();
    test_large_data();
    test_multiple_connections();
    test_mode_switching();
    test_error_handling();
    
    // Final cleanup
    cleanup_all_test_data();
    
    printf("All tests PASSED! ✅\n");
    return 0;
}