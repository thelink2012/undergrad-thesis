add_library(demo-sample-application-state SHARED
    sample.cpp
)
target_compile_features(demo-sample-application-state PUBLIC cxx_std_14)
target_compile_options(demo-sample-application-state PRIVATE ${JVMTIPROF_PEDANTIC_COMPILE_OPTIONS})
target_link_libraries(demo-sample-application-state jvmtiprof)
