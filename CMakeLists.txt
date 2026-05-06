cmake_minimum_required(VERSION 3.20)
project(tinyc C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Compiler warnings
add_compile_options(-Wall -Wextra -Wpedantic -Wno-unused-parameter -Wstrict-prototypes)

# Sanitizer support
option(ENABLE_ASAN "Build with AddressSanitizer + UBSan" OFF)
if(ENABLE_ASAN)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
endif()

# Main compiler executable
add_executable(tinyc
    src/main.c
    src/arena.c
    src/strtab.c
    src/diagnostic.c
    src/lexer.c
    src/type.c
    src/parser.c
    src/sema.c
    src/ir.c
    src/irgen.c
    src/regalloc.c
    src/codegen.c
    src/abi_classify.c
)
target_include_directories(tinyc PRIVATE include)

# Tests
enable_testing()
add_subdirectory(tests)
