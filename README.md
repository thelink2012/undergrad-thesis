# JVMTIPROF

**_JVMTIPROF: AN EXTENSION TO THE JAVA VIRTUAL MACHINE TOOL INFRASTRUCTURE FOR BUILDING PROFILING AGENTS_**

## Abstract

Building efficient profiling agents for the Java Virtual Machine is a challenging task. The job is hardened by most high-level instrumentation frameworks being written in and for use within the boundaries of the Java programming language. This work presents JVMTIPROF, an extension to the JVM Tool Infrastructure providing high-level functionality for instrumentation and profiling agents. JVMTIPROF can be used to intercept method calls and accurately sample the call stack of an application, all in native code. We evaluated our solution by replacing JVMTI in a profile-guided frequency scaling scheme and found no significant performance penalties.

## Thesis

The complete dissertation can be found in the [_pages deployment_](https://thelink2012.github.io/undergrad-thesis/main.pdf).

## Implementation

A prototype implementation can be found in the [_impl branch_](https://github.com/thelink2012/undergrad-thesis/tree/impl). It's incomplete and in most cases only the happy path works.

## Evaluation

The code for the evaluation can be found in [_raijenki/elastic-hurryup/hurryup-jvmtiprof_](https://github.com/raijenki/elastic-hurryup/tree/hurryup-jvmtiprof).

