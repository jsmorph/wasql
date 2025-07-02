#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <assert.h>
#include "block.h"

#define TEST_FILE "test_block_file"

// Helper function to clean up test files
void cleanup_test_files() {
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "rm -rf %s.blocks %s", TEST_FILE, TEST_FILE);
    system(cmd);
}

// Test basic open/close
void test_open_close() {
    printf("Testing open/close... ");
    
    cleanup_test_files();
    
    block_file_t *bf;
    assert(block_open(TEST_FILE, &bf) == 0);
    assert(bf != NULL);
    assert(bf->filename != NULL);
    assert(strcmp(bf->filename, TEST_FILE) == 0);
    
    assert(block_close(bf) == 0);
    
    printf("PASS\n");
}

// Test basic write and read
void test_write_read() {
    printf("Testing write/read... ");
    
    cleanup_test_files();
    
    block_file_t *bf;
    assert(block_open(TEST_FILE, &bf) == 0);
    
    // Write some data
    const char *data = "Hello, World!";
    int len = strlen(data);
    assert(block_write(bf, data, len, 0) == len);
    
    // Read it back
    char buffer[100];
    memset(buffer, 0, sizeof(buffer));
    assert(block_read(bf, buffer, len, 0) == len);
    assert(strcmp(buffer, data) == 0);
    
    block_close(bf);
    
    printf("PASS\n");
}

// Test reading non-existent data (should return zeros)
void test_read_zeros() {
    printf("Testing read zeros... ");
    
    cleanup_test_files();
    
    block_file_t *bf;
    assert(block_open(TEST_FILE, &bf) == 0);
    
    // Read from empty file
    char buffer[100];
    memset(buffer, 0xFF, sizeof(buffer)); // Fill with non-zero
    assert(block_read(bf, buffer, 50, 0) == 50);
    
    // Should be all zeros
    for (int i = 0; i < 50; i++) {
        assert(buffer[i] == 0);
    }
    
    block_close(bf);
    
    printf("PASS\n");
}

// Test cross-block operations
void test_cross_block() {
    printf("Testing cross-block operations... ");
    
    cleanup_test_files();
    
    block_file_t *bf;
    assert(block_open(TEST_FILE, &bf) == 0);
    
    // Write data that spans multiple blocks (4KB each)
    char large_data[8192];
    for (int i = 0; i < 8192; i++) {
        large_data[i] = (char)(i % 256);
    }
    
    assert(block_write(bf, large_data, 8192, 0) == 8192);
    
    // Read it back
    char read_buffer[8192];
    assert(block_read(bf, read_buffer, 8192, 0) == 8192);
    
    // Verify
    assert(memcmp(large_data, read_buffer, 8192) == 0);
    
    block_close(bf);
    
    printf("PASS\n");
}

// Test offset operations
void test_offset_operations() {
    printf("Testing offset operations... ");
    
    cleanup_test_files();
    
    block_file_t *bf;
    assert(block_open(TEST_FILE, &bf) == 0);
    
    // Write at various offsets
    const char *data1 = "AAAA";
    const char *data2 = "BBBB";
    const char *data3 = "CCCC";
    
    assert(block_write(bf, data1, 4, 100) == 4);
    assert(block_write(bf, data2, 4, 5000) == 4); // Different block
    assert(block_write(bf, data3, 4, 200) == 4);
    
    // Read back
    char buffer[10];
    
    memset(buffer, 0, sizeof(buffer));
    assert(block_read(bf, buffer, 4, 100) == 4);
    assert(memcmp(buffer, data1, 4) == 0);
    
    memset(buffer, 0, sizeof(buffer));
    assert(block_read(bf, buffer, 4, 5000) == 4);
    assert(memcmp(buffer, data2, 4) == 0);
    
    memset(buffer, 0, sizeof(buffer));
    assert(block_read(bf, buffer, 4, 200) == 4);
    assert(memcmp(buffer, data3, 4) == 0);
    
    // Check zeros between
    memset(buffer, 0xFF, sizeof(buffer));
    assert(block_read(bf, buffer, 4, 104) == 4);
    for (int i = 0; i < 4; i++) {
        assert(buffer[i] == 0);
    }
    
    block_close(bf);
    
    printf("PASS\n");
}

// Test file size calculation
void test_file_size() {
    printf("Testing file size... ");
    
    cleanup_test_files();
    
    block_file_t *bf;
    assert(block_open(TEST_FILE, &bf) == 0);
    
    // Empty file
    assert(block_file_size(bf) == 0);
    
    // Write some data
    const char *data = "Hello";
    assert(block_write(bf, data, 5, 0) == 5);
    assert(block_file_size(bf) == 4096); // Full block size
    
    // Write at a high offset
    assert(block_write(bf, data, 5, 8000) == 5);
    assert(block_file_size(bf) >= 8005);
    
    block_close(bf);
    
    printf("PASS\n");
}

// Test truncation
void test_truncate() {
    printf("Testing truncate... ");
    
    cleanup_test_files();
    
    block_file_t *bf;
    assert(block_open(TEST_FILE, &bf) == 0);
    
    // Write data across multiple blocks
    char data[10000];
    memset(data, 'X', sizeof(data));
    assert(block_write(bf, data, 10000, 0) == 10000);
    
    // Truncate to smaller size
    assert(block_truncate(bf, 5000) == 0);
    
    // Verify size
    long long size = block_file_size(bf);
    assert(size <= 5000 || size == 8192); // May round up to block boundary
    
    // Try to read beyond truncation point
    char buffer[100];
    memset(buffer, 0xFF, sizeof(buffer));
    assert(block_read(bf, buffer, 100, 5000) == 100);
    
    // Should be zeros
    for (int i = 0; i < 100; i++) {
        assert(buffer[i] == 0);
    }
    
    block_close(bf);
    
    printf("PASS\n");
}

// Test persistence across open/close
void test_persistence() {
    printf("Testing persistence... ");
    
    cleanup_test_files();
    
    // Write data
    block_file_t *bf1;
    assert(block_open(TEST_FILE, &bf1) == 0);
    
    const char *data = "Persistent data";
    int len = strlen(data);
    assert(block_write(bf1, data, len, 1000) == len);
    
    block_close(bf1);
    
    // Reopen and read
    block_file_t *bf2;
    assert(block_open(TEST_FILE, &bf2) == 0);
    
    char buffer[100];
    memset(buffer, 0, sizeof(buffer));
    assert(block_read(bf2, buffer, len, 1000) == len);
    assert(strcmp(buffer, data) == 0);
    
    block_close(bf2);
    
    printf("PASS\n");
}

int main() {
    printf("Running block I/O tests...\n\n");
    
    test_open_close();
    test_write_read();
    test_read_zeros();
    test_cross_block();
    test_offset_operations();
    test_file_size();
    test_truncate();
    test_persistence();
    
    cleanup_test_files();
    
    printf("\nAll tests passed!\n");
    return 0;
}