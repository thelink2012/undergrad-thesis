add_library(jvmtiprof STATIC
  env.cpp
  env_enter.cpp
  sampling_thread.cpp
  execution_sampler.cpp
  jvm/agent_thread.cpp
)
target_compile_features(jvmtiprof PUBLIC cxx_std_14)
target_compile_definitions(jvmtiprof PRIVATE JVMTIPROF_IMPLEMENTATION)
target_compile_options(jvmtiprof PRIVATE ${JVMTIPROF_PEDANTIC_COMPILE_OPTIONS})
set_property(TARGET jvmtiprof PROPERTY POSITION_INDEPENDENT_CODE ON)
target_link_libraries(jvmtiprof rt)
target_include_directories(jvmtiprof
  PUBLIC $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
  PUBLIC $<INSTALL_INTERFACE:include>
  PUBLIC ${JAVA_INCLUDE_PATH}
  PUBLIC ${JAVA_INCLUDE_PATH2})
