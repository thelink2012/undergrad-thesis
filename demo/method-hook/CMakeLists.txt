add_library(demo-method-hook SHARED
    hook.cpp
)
target_compile_features(demo-method-hook PUBLIC cxx_std_14)
target_compile_options(demo-method-hook PRIVATE ${JVMTIPROF_PEDANTIC_COMPILE_OPTIONS})
target_link_libraries(demo-method-hook jvmtiprof)
