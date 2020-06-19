#pragma once

#if __cplusplus >= 201703L
#define JVMTIPROF_NODISCARD [[nodiscard]]
#else
#define JVMTIPROF_NODISCARD __attribute__((warn_unused_result))
#endif

