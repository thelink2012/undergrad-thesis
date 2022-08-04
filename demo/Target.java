public class Target {
    private static volatile int value;

    public static void main(String[] args) throws Exception {
        System.out.println("Starting...");
        while(true) {
            ++value;
        }
    }
}
