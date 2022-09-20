public class Target {
    private static volatile int value;

    public static void printGreetings() {
        System.out.println("Starting ...");
    }

    public static void main(String[] args) throws Exception {
        printGreetings();
        while(true) {
            ++value;
        }
    }
}
