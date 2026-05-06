# Test executables
set(TEST_COMMON_SOURCES)
set(TEST_COMMON_LIBS)

# Shared source files for tests (link against the compiler sources directly)
set(TINYC_LIB_SOURCES
    ${CMAKE_SOURCE_DIR}/src/arena.c
    ${CMAKE_SOURCE_DIR}/src/strtab.c
    ${CMAKE_SOURCE_DIR}/src/diagnostic.c
    ${CMAKE_SOURCE_DIR}/src/lexer.c
    ${CMAKE_SOURCE_DIR}/src/type.c
    ${CMAKE_SOURCE_DIR}/src/parser.c
    ${CMAKE_SOURCE_DIR}/src/sema.c
    ${CMAKE_SOURCE_DIR}/src/ir.c
    ${CMAKE_SOURCE_DIR}/src/irgen.c
    ${CMAKE_SOURCE_DIR}/src/regalloc.c
    ${CMAKE_SOURCE_DIR}/src/codegen.c
    ${CMAKE_SOURCE_DIR}/src/abi_classify.c
)

# test_arena
add_executable(test_arena unit/test_arena.c ${TINYC_LIB_SOURCES})
target_include_directories(test_arena PRIVATE ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/tests)
add_test(NAME test_arena COMMAND test_arena)

# test_strtab
add_executable(test_strtab unit/test_strtab.c ${TINYC_LIB_SOURCES})
target_include_directories(test_strtab PRIVATE ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/tests)
add_test(NAME test_strtab COMMAND test_strtab)

# test_lexer
add_executable(test_lexer unit/test_lexer.c ${TINYC_LIB_SOURCES})
target_include_directories(test_lexer PRIVATE ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/tests)
add_test(NAME test_lexer COMMAND test_lexer)

# test_parser
add_executable(test_parser unit/test_parser.c ${TINYC_LIB_SOURCES})
target_include_directories(test_parser PRIVATE ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/tests)
add_test(NAME test_parser COMMAND test_parser)
