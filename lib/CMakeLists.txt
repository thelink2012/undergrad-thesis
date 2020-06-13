add_library(profagent STATIC
  env.cpp
  env_export.cpp
  agent.cpp
  perf.cpp
  phase.cpp
)
target_compile_features(profagent PUBLIC cxx_std_14)
target_compile_definitions(profagent PRIVATE JVMTIPROF_IMPLEMENTATION)
set_property(TARGET profagent PROPERTY POSITION_INDEPENDENT_CODE ON)
target_include_directories(profagent
  PUBLIC $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
  PUBLIC $<INSTALL_INTERFACE:include>
  PUBLIC ${JAVA_INCLUDE_PATH}
  PUBLIC ${JAVA_INCLUDE_PATH2})
