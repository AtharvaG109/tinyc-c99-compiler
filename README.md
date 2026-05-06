# tinyc - A Small C Compiler

A from-scratch C compiler targeting x86-64 (System V AMD64 ABI). The implementation is C11; the language front end currently targets a practical C99 subset with a verified self-hosting milestone.

## Architecture

```
Source file → gcc -E (preprocessor) → Lexer → Parser → Sema → IRgen → RegAlloc → Codegen → .s file → gcc/as+ld
```

## Building

```bash
# Primary workflow, no CMake required
make
make lint
make check
```

The CMake files are kept for environments that already have CMake installed:

```bash
cmake -B build -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Debug
make -C build

# With sanitizers
cmake -B build-asan -DENABLE_ASAN=ON -DCMAKE_BUILD_TYPE=Debug
make -C build-asan
```

## Running

```bash
# Lex a file and dump tokens
./build/tinyc --dump-tokens input.c

# Via the driver (preprocesses with gcc -E first)
./tools/tinyc-driver.sh input.c -o output.s
```

## Testing

```bash
# Build and run every unit and integration test
make check

# Run suites separately
make test
make test_integration
make test_abi

# Build tinyc with tinyc using the bundled self-host headers
make selfhost

# Deterministic stage comparison on macOS: disables linker UUID metadata
make selfhost LDFLAGS=-Wl,-no_uuid SELFHOST_OUT=/private/tmp/tinyc-selfhost-stage1
```

Manual smoke test:

```bash
./build/tinyc -S tests/integration/structs.c -o /tmp/structs.s
as /tmp/structs.s -o /tmp/structs.o
cc /tmp/structs.o -o /tmp/structs
/tmp/structs
```

## Configuration

tinyc does not require runtime environment variables for normal builds or tests.

| Variable | Required | Default | Description |
|----------|----------|---------|-------------|
| `CC` | No | `cc` | C compiler used to build tinyc and test binaries. |
| `CFLAGS` | No | `-std=c11 -Wall -Wextra -Wpedantic -Wno-unused-parameter -g -Iinclude` | Compiler flags for local builds. |
| `LDFLAGS` | No | empty | Linker flags, including deterministic macOS self-host flags. |
| `SELFHOST_DIR` | No | `/private/tmp/tinyc-selfhost-build` | Scratch directory for self-host preprocessed, assembly, and object files. |
| `SELFHOST_OUT` | No | `/private/tmp/tinyc-selfhost` | Output path for the self-hosted compiler binary. |

## C99 Coverage

**Validated by tests:** integer arithmetic, floating-point arithmetic/calls/returns, functions, recursion, function pointers, function-pointer typedefs, typedef-name declarations in block scope, storage classes, qualifiers including const-write rejection, variadic calls, pointers, arrays, bounded local VLA-style indexing, local and global aggregate initializers, structs, unions, simple integer bit-fields, struct/union assignment, aggregate designators, compound literals, `sizeof`, globals, string literals, `if`, `for`, `switch`, `goto`, calls to external functions, small and memory-class struct pass/return ABI cases, `_Pragma` no-op parsing, `_Complex`/`_Imaginary` keyword parsing as scalar floating aliases, preprocessor line-marker skipping, and parser coverage for common C99 declarations and expressions.

**Implemented but still needs broader validation:** enums, additional function-pointer typedef forms, deeper qualifier semantics, larger multi-file/library-style programs, compound bit-field assignments, global bit-field aggregate initialization, broader VLA behavior such as runtime `sizeof(vla)`, and more ABI edge cases beyond the current scalar/SSE two-eightbyte aggregate path.

**Self-hosting status:** `make selfhost` builds a tinyc binary with the repository's bundled self-host headers. On macOS, bit-for-bit stage comparison requires linking both stages with `-Wl,-no_uuid`; otherwise the linker embeds per-build UUID metadata even when the generated text is identical.

## License

MIT
