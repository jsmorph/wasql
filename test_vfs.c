#include "sqlite3.h"
#include <stdio.h>

#define n 1000

// Forward declarations from your VFS
extern int sqlite3_loggingvfs_init(const char *logFilePath);
extern int sqlite3_loggingvfs_shutdown(void);
extern void sqlite3_loggingvfs_set_block_storage(int enable);

int main() {
    sqlite3 *db;
    int rc;
    
    printf("Initializing logging VFS ...\n");
    
    // Enable block storage
    printf("Enabling block storage ...\n");
    sqlite3_loggingvfs_set_block_storage(1);
    
    rc = sqlite3_loggingvfs_init("operations.log");
    if (rc != SQLITE_OK) {
        printf("Failed to initialize VFS, error code: %d\n", rc);
        return 1;
    }
    
    printf("Opening database with logging VFS ...\n");
    rc = sqlite3_open_v2("test.db", &db, 
                         SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, 
                         "logging");
    
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Error opening database: %s\n", sqlite3_errmsg(db));
        return 1;
    }
    
    printf("Creating table ...\n");
    sqlite3_exec(db, "CREATE TABLE users(id INTEGER, name TEXT)", 0, 0, 0);
    
    printf("Inserting data ...\n");
    for (int i = 1; i <= n; i++) {
        char sql[256];
        snprintf(sql, sizeof(sql), "INSERT INTO users VALUES(%d, 'User_%d')", i, i);
        sqlite3_exec(db, sql, 0, 0, 0);
    }
    
    printf("Deletiing data ...\n");
    rc = sqlite3_exec(db, "DELETE FROM users WHERE id <= 10", 0, 0, 0);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Error %s: DELETE FROM \n", sqlite3_errmsg(db));
        return 1;
    }
    
    printf("Querying data (first 5 records) ...\n");
    sqlite3_stmt *stmt_select;
    rc = sqlite3_prepare_v2(db, "SELECT * FROM users LIMIT 5", -1, &stmt_select, NULL);
    if (rc == SQLITE_OK) {
        while ((rc = sqlite3_step(stmt_select)) == SQLITE_ROW) {
            int id = sqlite3_column_int(stmt_select, 0);
            const char *name = (const char*)sqlite3_column_text(stmt_select, 1);
            printf("ID: %d, Name: %s\n", id, name);
        }
        sqlite3_finalize(stmt_select);
    }
    
    printf("Getting COUNT ...\n");
    sqlite3_stmt *stmt;
    rc = sqlite3_prepare_v2(db, "SELECT COUNT(id) FROM users", -1, &stmt, NULL);
    if (rc == SQLITE_OK) {
        rc = sqlite3_step(stmt);
        if (rc == SQLITE_ROW) {
            int max_id = sqlite3_column_int(stmt, 0);
            printf("COUNT: %d\n", max_id);
        }
        sqlite3_finalize(stmt);
    }
    
    sqlite3_close(db);
    
    printf("Reopening database to verify persistence ...\n");
    rc = sqlite3_open_v2("test.db", &db, 
                         SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, 
                         "logging");
    
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Error reopening database: %s\n", sqlite3_errmsg(db));
        return 1;
    }
    
    printf("Getting COUNT from reopened database ...\n");
    sqlite3_stmt *stmt2;
    rc = sqlite3_prepare_v2(db, "SELECT COUNT(id) FROM users", -1, &stmt2, NULL);
    if (rc == SQLITE_OK) {
        rc = sqlite3_step(stmt2);
        if (rc == SQLITE_ROW) {
            int max_id = sqlite3_column_int(stmt2, 0);
            printf("COUNT after reopening: %d\n", max_id);
        }
        sqlite3_finalize(stmt2);
    }
    
    sqlite3_close(db);
    sqlite3_loggingvfs_shutdown();
    
    printf("Done! Check operations.log for the logged operations\n");
    return 0;
}
