public class JVMTIPROF {
    public static native void onMethodEntry(long hookPtr);
    public static native void onMethodExit(long hookPtr);
}
