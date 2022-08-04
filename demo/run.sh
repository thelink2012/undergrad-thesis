#!/bin/bash
# TODO temporary, move to CMakeLists
set -euo pipefail
cd "$(dirname "$0")"
javac Target.java
java -cp "$(pwd)" "-agentpath:$(pwd)/../build/demo/$1/libdemo-$1.so" Target
