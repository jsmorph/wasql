#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>
#include "block.h"

#define BLOCK_SIZE 4096
#define MAX_PATH_LEN 1024

// Get the directory path for a file's blocks
static void get_block_dir(const char *filename, char *block_dir) {
    snprintf(block_dir, MAX_PATH_LEN, "%s.blocks", filename);
}

// Get the path for a specific block file
static int get_block_path(const char *filename, int block_num, char *block_path) {
    char block_dir[MAX_PATH_LEN];
    get_block_dir(filename, block_dir);
    int result = snprintf(block_path, MAX_PATH_LEN, "%s/block_%06d", block_dir, block_num);
    return (result >= MAX_PATH_LEN) ? -1 : 0;
}

// Ensure the block directory exists
static int ensure_block_dir(const char *filename) {
    char block_dir[MAX_PATH_LEN];
    get_block_dir(filename, block_dir);
    
    struct stat st;
    if (stat(block_dir, &st) == -1) {
        if (mkdir(block_dir, 0755) == -1) {
            return -1;
        }
    }
    return 0;
}

int block_open(const char *filename, block_file_t **bf) {
    *bf = malloc(sizeof(block_file_t));
    if (!*bf) {
        return -1;
    }
    
    (*bf)->filename = strdup(filename);
    if (!(*bf)->filename) {
        free(*bf);
        return -1;
    }
    
    if (ensure_block_dir(filename) != 0) {
        free((*bf)->filename);
        free(*bf);
        return -1;
    }
    
    return 0;
}

int block_close(block_file_t *bf) {
    if (!bf) return 0;
    
    free(bf->filename);
    free(bf);
    return 0;
}

int block_read(block_file_t *bf, void *buffer, int size, long long offset) {
    if (!bf || !buffer || size < 0 || offset < 0) {
        return -1;
    }
    
    char *buf = (char *)buffer;
    int total_read = 0;
    
    while (size > 0) {
        int block_num = offset / BLOCK_SIZE;
        int block_offset = offset % BLOCK_SIZE;
        int to_read = (size < BLOCK_SIZE - block_offset) ? size : BLOCK_SIZE - block_offset;
        
        char block_path[MAX_PATH_LEN];
        if (get_block_path(bf->filename, block_num, block_path) != 0) {
            return -1;
        }
        
        FILE *block_file = fopen(block_path, "rb");
        if (!block_file) {
            // Block doesn't exist, fill with zeros
            memset(buf, 0, to_read);
        } else {
            if (fseek(block_file, block_offset, SEEK_SET) != 0) {
                fclose(block_file);
                return -1;
            }
            
            int bytes_read = fread(buf, 1, to_read, block_file);
            if (bytes_read < to_read) {
                // Partial read, fill remainder with zeros
                memset(buf + bytes_read, 0, to_read - bytes_read);
            }
            fclose(block_file);
        }
        
        buf += to_read;
        offset += to_read;
        size -= to_read;
        total_read += to_read;
    }
    
    return total_read;
}

int block_write(block_file_t *bf, const void *buffer, int size, long long offset) {
    if (!bf || !buffer || size < 0 || offset < 0) {
        return -1;
    }
    
    const char *buf = (const char *)buffer;
    int total_written = 0;
    
    while (size > 0) {
        int block_num = offset / BLOCK_SIZE;
        int block_offset = offset % BLOCK_SIZE;
        int to_write = (size < BLOCK_SIZE - block_offset) ? size : BLOCK_SIZE - block_offset;
        
        char block_path[MAX_PATH_LEN];
        if (get_block_path(bf->filename, block_num, block_path) != 0) {
            return -1;
        }
        
        
        // If we're doing a partial block write, we need to read-modify-write
        if (block_offset != 0 || to_write != BLOCK_SIZE) {
            char block_data[BLOCK_SIZE];
            memset(block_data, 0, BLOCK_SIZE);
            
            // Try to read existing block data
            FILE *existing = fopen(block_path, "rb");
            if (existing) {
                fread(block_data, 1, BLOCK_SIZE, existing);
                fclose(existing);
            }
            
            // Modify the relevant portion
            memcpy(block_data + block_offset, buf, to_write);
            
            // Write the entire block
            FILE *block_file = fopen(block_path, "wb");
            if (!block_file) {
                return -1;
            }
            
            if (fwrite(block_data, 1, BLOCK_SIZE, block_file) != BLOCK_SIZE) {
                fclose(block_file);
                return -1;
            }
            fclose(block_file);
        } else {
            // Full block write
            FILE *block_file = fopen(block_path, "wb");
            if (!block_file) {
                return -1;
            }
            
            if (fwrite(buf, 1, to_write, block_file) != to_write) {
                fclose(block_file);
                return -1;
            }
            fclose(block_file);
        }
        
        
        buf += to_write;
        offset += to_write;
        size -= to_write;
        total_written += to_write;
    }
    
    return total_written;
}

int block_truncate(block_file_t *bf, long long size) {
    if (!bf || size < 0) {
        return -1;
    }
    
    int last_block = (size + BLOCK_SIZE - 1) / BLOCK_SIZE;
    
    // Remove blocks beyond the truncation point
    char block_dir[MAX_PATH_LEN];
    get_block_dir(bf->filename, block_dir);
    
    // Simple approach: try to remove blocks up to a reasonable limit
    for (int block_num = last_block; block_num < 10000; block_num++) {
        char block_path[MAX_PATH_LEN];
        if (get_block_path(bf->filename, block_num, block_path) != 0) {
            return -1;
        }
        
        if (unlink(block_path) != 0) {
            // If we can't remove it, it probably doesn't exist
            if (errno != ENOENT) {
                break;
            }
        }
    }
    
    // Handle partial last block
    if (size > 0 && (size % BLOCK_SIZE) != 0) {
        int last_block_num = (size - 1) / BLOCK_SIZE;
        int last_block_size = size % BLOCK_SIZE;
        
        char block_path[MAX_PATH_LEN];
        get_block_path(bf->filename, last_block_num, block_path);
        
        // Read existing block data
        char block_data[BLOCK_SIZE];
        memset(block_data, 0, BLOCK_SIZE);
        
        FILE *existing = fopen(block_path, "rb");
        if (existing) {
            fread(block_data, 1, BLOCK_SIZE, existing);
            fclose(existing);
        }
        
        // Write truncated block
        FILE *block_file = fopen(block_path, "wb");
        if (block_file) {
            fwrite(block_data, 1, last_block_size, block_file);
            fclose(block_file);
        }
    }
    
    return 0;
}

long long block_file_size(block_file_t *bf) {
    if (!bf) {
        return -1;
    }
    
    char block_dir[MAX_PATH_LEN];
    get_block_dir(bf->filename, block_dir);
    
    long long max_size = 0;
    
    // Check blocks up to a reasonable limit
    for (int block_num = 0; block_num < 10000; block_num++) {
        char block_path[MAX_PATH_LEN];
        if (get_block_path(bf->filename, block_num, block_path) != 0) {
            return -1;
        }
        
        struct stat st;
        if (stat(block_path, &st) == 0) {
            long long block_end = (long long)(block_num + 1) * BLOCK_SIZE;
            if (st.st_size < BLOCK_SIZE) {
                // Partial block, calculate exact end
                block_end = (long long)block_num * BLOCK_SIZE + st.st_size;
            }
            if (block_end > max_size) {
                max_size = block_end;
            }
        }
    }
    
    return max_size;
}