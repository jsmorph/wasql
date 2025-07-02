#ifndef BLOCK_H
#define BLOCK_H

typedef struct {
    char *filename;
} block_file_t;

// Open a block-oriented file
int block_open(const char *filename, block_file_t **bf);

// Close a block-oriented file
int block_close(block_file_t *bf);

// Read from a block-oriented file
int block_read(block_file_t *bf, void *buffer, int size, long long offset);

// Write to a block-oriented file
int block_write(block_file_t *bf, const void *buffer, int size, long long offset);

// Truncate a block-oriented file
int block_truncate(block_file_t *bf, long long size);

// Get the size of a block-oriented file
long long block_file_size(block_file_t *bf);

#endif // BLOCK_H