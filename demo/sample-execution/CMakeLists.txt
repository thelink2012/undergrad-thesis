add_library(demo-sample-execution SHARED
    sample.cpp
)
target_compile_features(demo-sample-execution PUBLIC cxx_std_14)
target_compile_options(demo-sample-execution PRIVATE ${JVMTIPROF_PEDANTIC_COMPILE_OPTIONS})
target_link_libraries(demo-sample-execution jvmtiprof)
