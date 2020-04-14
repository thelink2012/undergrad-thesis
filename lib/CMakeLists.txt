add_library(schedprof STATIC
  agent.cpp
  perf.cpp
  phase.cpp
)
target_compile_features(schedprof PUBLIC cxx_std_14)
target_include_directories(schedprof
  PUBLIC $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
  PUBLIC $<BUILD_INTERFACE:${JAVA_HOME}/include>
  PUBLIC $<BUILD_INTERFACE:${JAVA_HOME}/include/linux>
  PUBLIC $<INSTALL_INTERFACE:include>)
