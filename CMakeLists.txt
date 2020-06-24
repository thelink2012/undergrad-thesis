cmake_minimum_required(VERSION 3.5)
project(profagent)

find_package(JNI REQUIRED)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU" OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  set(JVMTIPROF_PEDANTIC_COMPILE_OPTIONS -pedantic-errors -pedantic -Wall -Wextra -Werror
    -Wcast-align -Wdouble-promotion -Wno-unused-variable -Wno-missing-field-initializers
    -Wno-unused-parameter -Wno-error=invalid-offsetof)
endif()

add_subdirectory(lib)
