#include <stdio.h>
#include <stdlib.h>
#include "sqlite3.h"

extern int sqlite3_loggingvfs_init(const char *logFilePath);
extern int sqlite3_loggingvfs_shutdown(void);
extern void sqlite3_loggingvfs_set_block_storage(int enable);

int main() {
    printf("Debug VFS test\n");
    
    // Clean up first
    system("rm -rf debug_test.db* debug_test.log");
    
    int rc = sqlite3_loggingvfs_init("debug_test.log");
    printf("VFS init result: %d\n", rc);
    
    if (rc != SQLITE_OK) {
        printf("VFS init failed!\n");
        return 1;
    }
    
    sqlite3_loggingvfs_set_block_storage(1);
    printf("Block storage enabled\n");
    
    sqlite3 *db;
    rc = sqlite3_open_v2("debug_test.db", &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, "logging");
    printf("Database open result: %d\n", rc);
    
    if (rc != SQLITE_OK) {
        printf("Database open failed: %s\n", sqlite3_errmsg(db));
        return 1;
    }
    
    printf("Database opened successfully\n");
    
    // Try a simple operation
    char *errmsg = NULL;
    rc = sqlite3_exec(db, "SELECT 1", NULL, NULL, &errmsg);
    printf("Simple query result: %d\n", rc);
    
    if (rc != SQLITE_OK) {
        printf("Simple query failed: %s\n", errmsg ? errmsg : "Unknown error");
        if (errmsg) sqlite3_free(errmsg);
    } else {
        printf("Simple query succeeded\n");
    }
    
    sqlite3_close(db);
    sqlite3_loggingvfs_shutdown();
    
    printf("Test complete\n");
    return 0;
}