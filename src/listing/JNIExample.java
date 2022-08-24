package com.waldo;

class JNIExample
{
  native int foo(int bar);

  static {
    System.
      loadLibrary("qux");
  }
}
