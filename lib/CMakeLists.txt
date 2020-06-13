add_library(jvmtiprof STATIC
  env.cpp
  env_export.cpp
)
target_compile_features(jvmtiprof PUBLIC cxx_std_14)
target_compile_definitions(jvmtiprof PRIVATE JVMTIPROF_IMPLEMENTATION)
set_property(TARGET jvmtiprof PROPERTY POSITION_INDEPENDENT_CODE ON)
target_include_directories(jvmtiprof
  PUBLIC $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
  PUBLIC $<INSTALL_INTERFACE:include>
  PUBLIC ${JAVA_INCLUDE_PATH}
  PUBLIC ${JAVA_INCLUDE_PATH2})
