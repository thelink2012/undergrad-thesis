add_library(demo-hello-jvmtiprof SHARED
    hello.cpp
)
target_compile_features(demo-hello-jvmtiprof PUBLIC cxx_std_14)
target_compile_options(demo-hello-jvmtiprof PRIVATE ${JVMTIPROF_PEDANTIC_COMPILE_OPTIONS})
target_link_libraries(demo-hello-jvmtiprof jvmtiprof)
