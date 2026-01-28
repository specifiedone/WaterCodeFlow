public class CallbackMain {
    /**
     * Callback function for memwatch CLI (Java)
     * 
     * This function is called whenever a memory change is detected.
     * The method MUST be named 'main' for memwatch to invoke it.
     * 
     * Usage:
     *   memwatch run java -jar app.jar --user-func CallbackMain.java --user-func-lang java
     */
    
    public static void main(String[] args) {
        System.out.println("🔔 [Callback] Memory change detected!");
        System.out.println("   This callback was triggered by memwatch CLI");
        System.out.println("   You can add custom logic here:");
        System.out.println("   - Log changes to file");
        System.out.println("   - Alert on suspicious activity");
        System.out.println("   - Analyze memory patterns");
        System.out.println("   - Integrate with monitoring");
        
        // Read event from file if available
        try {
            java.nio.file.Path eventFile = java.nio.file.Paths.get("/tmp/memwatch_event_latest.json");
            if (java.nio.file.Files.exists(eventFile)) {
                String content = new String(java.nio.file.Files.readAllBytes(eventFile));
                System.out.println("   Event: " + content);
            }
        } catch (Exception e) {
            System.err.println("   Error reading event: " + e.getMessage());
        }
    }
}
