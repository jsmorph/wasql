all: test_vfs.wasm

test_vfs.wasm:	Makefile logging_vfs.c block.c test_vfs.c
	$$WASI_SDK_PATH/bin/clang \
	  --sysroot=$$WASI_SDK_PATH/share/wasi-sysroot \
	  -DSQLITE_THREADSAFE=0 \
	  -DSQLITE_OMIT_LOAD_EXTENSION \
	  -Isqlite-amalgamation-3450000 \
	  -o test_vfs.wasm \
	  sqlite-amalgamation-3450000/sqlite3.c logging_vfs.c block.c test_vfs.c

test:	Makefile clean test_vfs.wasm
	wasmtime --dir=. test_vfs.wasm

test_vfs_simple.wasm: test_vfs_simple.c logging_vfs.c block.c sqlite-amalgamation-3450000/sqlite3.c
	$$WASI_SDK_PATH/bin/clang \
	  --sysroot=$$WASI_SDK_PATH/share/wasi-sysroot \
	  -DSQLITE_THREADSAFE=0 \
	  -DSQLITE_OMIT_LOAD_EXTENSION \
	  -Isqlite-amalgamation-3450000 \
	  -o test_vfs_simple.wasm \
	  sqlite-amalgamation-3450000/sqlite3.c logging_vfs.c block.c test_vfs_simple.c

test_vfs_comprehensive.wasm: test_vfs_comprehensive.c logging_vfs.c block.c sqlite-amalgamation-3450000/sqlite3.c
	$$WASI_SDK_PATH/bin/clang \
	  --sysroot=$$WASI_SDK_PATH/share/wasi-sysroot \
	  -DSQLITE_THREADSAFE=0 \
	  -DSQLITE_OMIT_LOAD_EXTENSION \
	  -Isqlite-amalgamation-3450000 \
	  -o test_vfs_comprehensive.wasm \
	  sqlite-amalgamation-3450000/sqlite3.c logging_vfs.c block.c test_vfs_comprehensive.c

test_block.wasm: test_block.c block.c
	$$WASI_SDK_PATH/bin/clang \
	  --sysroot=$$WASI_SDK_PATH/share/wasi-sysroot \
	  -o test_block.wasm \
	  block.c test_block.c

# Note: WASM tests are limited by WASI capabilities
# Tests that use system() calls cannot run under WASM
# Set WASI_SDK_PATH environment variable before building

run_wasm_simple: test_vfs_simple.wasm
	@echo "Note: This test uses system() calls which may not work in WASM"
	wasmtime --dir=. test_vfs_simple.wasm

run_wasm_comprehensive: test_vfs_comprehensive.wasm  
	@echo "Note: This test uses system() calls which may not work in WASM"
	wasmtime --dir=. test_vfs_comprehensive.wasm

run_wasm_block: test_block.wasm
	@echo "Note: This test uses system() calls which may not work in WASM"
	wasmtime --dir=. test_block.wasm

# Basic VFS test that works reliably under WASM (no system() calls)
run_wasm: all
	wasmtime --dir=. test_vfs.wasm

test_block: test_block.c block.c block.h
	gcc -o test_block test_block.c block.c

run_block_test: test_block
	./test_block

test_vfs_comprehensive: test_vfs_comprehensive.c logging_vfs.c block.c sqlite-amalgamation-3450000/sqlite3.c
	gcc -o test_vfs_comprehensive test_vfs_comprehensive.c logging_vfs.c block.c sqlite-amalgamation-3450000/sqlite3.c -Isqlite-amalgamation-3450000 -DSQLITE_THREADSAFE=0 -DSQLITE_OMIT_LOAD_EXTENSION

run_comprehensive_test: test_vfs_comprehensive
	./test_vfs_comprehensive

test_vfs_simple: test_vfs_simple.c logging_vfs.c block.c sqlite-amalgamation-3450000/sqlite3.c
	gcc -o test_vfs_simple test_vfs_simple.c logging_vfs.c block.c sqlite-amalgamation-3450000/sqlite3.c -Isqlite-amalgamation-3450000 -DSQLITE_THREADSAFE=0 -DSQLITE_OMIT_LOAD_EXTENSION

run_simple_test: test_vfs_simple
	./test_vfs_simple

clean:
	rm -f *.wasm
	rm -f test_block test_vfs_native test_vfs_comprehensive test_vfs_simple debug_vfs
	rm -f *.log
	rm -f test*.db test*.db-journal test*.db-wal test*.db-shm
	rm -rf *.blocks
	rm -f simple_test.* debug_test.* regular_* block_*
	rm -rf test_*.blocks debug_*.blocks regular_*.blocks block_*.blocks

# Build and run all native tests from scratch
test_all_native: clean run_block_test run_simple_test run_comprehensive_test
	@echo "All native tests completed successfully!"

# Build and run all WASM tests from scratch  
test_all_wasm: clean run_wasm
	@echo "All WASM tests completed successfully!"

# Build and run everything from scratch
test_everything: clean run_block_test run_simple_test run_comprehensive_test run_wasm
	@echo "All tests (native and WASM) completed successfully!"
