# SQLite Block Storage VFS

A custom SQLite Virtual File System (VFS) implementation that stores database files as individual 4KB block files, with comprehensive logging and runtime mode switching.

## Goals

The primary goal of this project is to experiment compiling SQLite to run in WASM with a low-level API that does block I/O operations.  Each block is stored as its own file.  The I/O uses WASI is WASM, but that approach could presumably be swapped out more surgically.

### Development

1. Started with a simple "logging" VFS - Created a pass-through VFS wrapper that logs all SQLite file operations while delegating to the default VFS
2. Implemented the block storage layer - Built a standalone block I/O API (`block.c`) that handles 4KB blocks stored as individual files
3. Modified the VFS to use block storage - Integrated the block layer into the logging VFS with runtime switching between block and regular storage modes

## Architecture

### Block Storage Layer (`block.c`)
- Block Size: 4KB (configurable via `BLOCK_SIZE`)
- Storage Format: `filename.blocks/block_XXXXXX` (6-digit zero-padded)
- Operations: Read, write, truncate, file size calculation
- Zero-fill: Automatic zero-filling for unwritten areas
- Cross-block I/O: Seamless operations spanning multiple blocks

### VFS Layer (`logging_vfs.c`)
- Base VFS: Wraps default SQLite VFS with logging
- Runtime Switching: `sqlite3_loggingvfs_set_block_storage(int enable)`
- Compliance: Full SQLite VFS specification compliance
- Logging: Comprehensive operation logging with timestamps

## API

```c
// Initialize VFS
int sqlite3_loggingvfs_init(const char *logFilePath);

// Enable/disable block storage
void sqlite3_loggingvfs_set_block_storage(int enable);

// Shutdown VFS
int sqlite3_loggingvfs_shutdown(void);

// Block storage API
int block_open(const char *filename, block_file_t **bf);
int block_read(block_file_t *bf, void *buffer, int size, long long offset);
int block_write(block_file_t *bf, const void *buffer, int size, long long offset);
int block_truncate(block_file_t *bf, long long size);
long long block_file_size(block_file_t *bf);
int block_close(block_file_t *bf);
```

## Usage

```c
#include "sqlite3.h"

int main() {
    sqlite3 *db;
    
    // Initialize logging VFS
    sqlite3_loggingvfs_init("operations.log");
    
    // Enable block storage mode
    sqlite3_loggingvfs_set_block_storage(1);
    
    // Open database using block storage
    sqlite3_open_v2("mydb.db", &db, 
                     SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE,
                     "logging");
    
    // Standard SQLite operations
    sqlite3_exec(db, "CREATE TABLE users(id INTEGER, name TEXT)", 0, 0, 0);
    
    sqlite3_close(db);
    sqlite3_loggingvfs_shutdown();
    return 0;
}
```

## Build

### Native
```bash
make test_block               # Block I/O tests
make run_simple_test          # Basic VFS tests  
make run_comprehensive_test   # Full test suite
```

### WebAssembly
```bash
make test                     # Build and run WASM version
```

## Implementation Details

### VFS Method Mapping
- Block Mode: All file operations route through block storage layer
- Regular Mode: Pass-through to default VFS
- Locking: No-op for block storage (always returns `SQLITE_OK`)
- File Control: Returns `SQLITE_NOTFOUND` for block storage
- Device Characteristics: `SQLITE_IOCAP_ATOMIC4K | SQLITE_IOCAP_SAFE_APPEND`

### SQLite Compliance
- Read Operations: Zero-fill beyond EOF, return `SQLITE_OK`
- Write Operations: Atomic 4KB block updates
- Truncation: Remove unnecessary blocks, handle partial last block
- Journaling: Supported (journal files also use block storage)

### Error Handling
- Path Length: 1024 character limit with overflow detection
- I/O Errors: Proper SQLite error code mapping
- Memory Management: All allocations tracked and freed
- Cleanup: Block directories removed on file deletion

## Testing

The implementation includes comprehensive test coverage:

- Unit Tests: Block I/O operations (8 tests)
- Integration Tests: VFS functionality (7 tests)  
- Persistence Tests: Cross-session data integrity
- Concurrency Tests: Multiple database connections
- Error Tests: Graceful failure handling
- Mode Tests: Block vs regular VFS switching

All tests perform complete cleanup before execution to ensure isolation.

## Dependencies

### SQLite Amalgamation

Download SQLite 3.45.0 amalgamation:

```bash
# Download and extract SQLite amalgamation
wget https://www.sqlite.org/2024/sqlite-amalgamation-3450000.zip
unzip sqlite-amalgamation-3450000.zip
```

The project expects the amalgamation in `sqlite-amalgamation-3450000/` directory containing:
- `sqlite3.h` - SQLite header file
- `sqlite3.c` - SQLite implementation

### Build Tools

- Native builds: C99 compiler (GCC/Clang)
- WebAssembly builds: [WASI SDK](https://github.com/WebAssembly/wasi-sdk)
- Runtime: POSIX-compatible file system

### WASI SDK Setup

```bash
# Download WASI SDK (adjust version as needed)
wget https://github.com/WebAssembly/wasi-sdk/releases/download/wasi-sdk-20/wasi-sdk-20.0-linux.tar.gz
tar xzf wasi-sdk-20.0-linux.tar.gz
export WASI_SDK_PATH=/path/to/wasi-sdk-20.0

# Verify installation
$WASI_SDK_PATH/bin/clang --version
```

## Performance Characteristics

- Read Amplification: 4KB minimum read unit
- Write Amplification: Read-modify-write for partial blocks
- Storage Overhead: Directory structure per file
- Concurrency: No file-level locking (application-managed)

## References

1. https://www.sqlite.org/vfs.html
1. https://www.sqlite.org/wasm/doc/trunk/index.md
1. https://wasi.dev/

