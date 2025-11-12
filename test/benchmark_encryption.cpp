#include <Arduino.h>
#include <esp_timer.h>
#include <esp_heap_caps.h>
#include <esp_task_wdt.h>

// Include nostr library files directly
#include "../lib/nostr/nostr.h"
#include "../lib/aes/aes.h"

namespace BenchmarkEncryption {

struct BenchmarkResult {
    const char* testName;
    size_t dataSize;
    unsigned long executionTimeMs;
    size_t heapBefore;
    size_t heapAfter;
    size_t heapDelta;
    double throughputBytesPerSec;
    bool success;
};

struct TestData {
    String message;
    String privateKeyHex;
    String publicKeyHex;
    byte key[32];
    byte iv[16];
};

class EncryptionBenchmark {
private:
    static const int MAX_RESULTS = 50;
    BenchmarkResult results[MAX_RESULTS];
    int resultCount = 0;
    TestData testData;
    unsigned long benchmarkStart;
    size_t heapBefore;

    void initializeTestData() {
        testData.privateKeyHex = "b6ea49b7e0d5e2d9d4f6c3a8e7f1b2c3d4e5f6a7b8c9d0e1f2a3b4c5d6e7f8";
        testData.publicKeyHex = "c7f2a8d5e1b4c9f6a3e8d2b7c5a9f1e4d7c3b8f5a2e6d9c4b1f7e3a6c8d5b2";
        
        for (int i = 0; i < 32; i++) {
            testData.key[i] = i * 8 + 17; // Deterministic key
        }
        for (int i = 0; i < 16; i++) {
            testData.iv[i] = i * 16 + 33; // Deterministic IV
        }
    }

    void startBenchmark(const char* testName) {
        heapBefore = esp_get_free_heap_size();
        benchmarkStart = millis();
        esp_task_wdt_reset(); // Reset watchdog
        Serial.print("Starting benchmark: ");
        Serial.println(testName);
    }

    void endBenchmark(const char* testName, size_t dataSize, bool success) {
        unsigned long duration = millis() - benchmarkStart;
        size_t heapAfter = esp_get_free_heap_size();
        size_t heapDelta = heapBefore > heapAfter ? heapBefore - heapAfter : 0;
        double throughput = success && duration > 0 ? (dataSize * 1000.0) / duration : 0;

        if (resultCount < MAX_RESULTS) {
            results[resultCount] = {
                testName,
                dataSize,
                duration,
                heapBefore,
                heapAfter,
                heapDelta,
                throughput,
                success
            };
            resultCount++;
        }

        Serial.printf("  Duration: %lu ms, Heap: %d bytes used, Throughput: %.2f B/s\n", 
                     duration, (int)heapDelta, throughput);
    }

    String generateTestMessage(size_t size) {
        String msg = "";
        msg.reserve(size + 1);
        
        const char* baseText = "This is a test message for encryption benchmarking. ";
        size_t baseLen = strlen(baseText);
        
        while (msg.length() < size) {
            size_t remaining = size - msg.length();
            if (remaining >= baseLen) {
                msg += baseText;
            } else {
                msg += String(baseText).substring(0, remaining);
            }
        }
        
        return msg;
    }

public:
    void setup() {
        Serial.begin(115200);
        delay(2000);
        Serial.println("\n=== Nostr Encryption/Decryption Benchmark ===");
        Serial.printf("Free heap: %d bytes\n", esp_get_free_heap_size());
        Serial.printf("PSRAM size: %d bytes\n", esp_himem_get_phys_size());
        
        initializeTestData();
        nostr::initMemorySpace(8192, 16384); // Initialize nostr memory
        
        Serial.println("Test data initialized");
    }

    void benchmarkAESDecryptData() {
        Serial.println("\n--- AES decryptData() Benchmark ---");
        
        size_t testSizes[] = {16, 64, 256, 1024, 4096, 8192};
        int numSizes = sizeof(testSizes) / sizeof(testSizes[0]);

        for (int i = 0; i < numSizes; i++) {
            size_t size = testSizes[i];
            
            // Ensure size is multiple of 16 for AES
            size_t paddedSize = ((size + 15) / 16) * 16;
            
            // Allocate and prepare test data
            byte* testData = (byte*)malloc(paddedSize);
            if (!testData) {
                Serial.printf("Failed to allocate %d bytes for test\n", paddedSize);
                continue;
            }
            
            // Fill with test pattern
            for (size_t j = 0; j < paddedSize; j++) {
                testData[j] = (j % 256);
            }
            
            String testName = "AES Decrypt " + String(size) + "B";
            
            startBenchmark(testName.c_str());
            String result = nostr::decryptData(this->testData.key, this->testData.iv, testData, paddedSize);
            bool success = result.length() > 0;
            endBenchmark(testName.c_str(), size, success);
            
            free(testData);
            delay(100); // Allow heap recovery
        }
    }

    void benchmarkAESEncryptData() {
        Serial.println("\n--- AES encryptData() Benchmark ---");
        
        size_t testSizes[] = {16, 64, 256, 1024, 4096, 8192};
        int numSizes = sizeof(testSizes) / sizeof(testSizes[0]);

        for (int i = 0; i < numSizes; i++) {
            size_t size = testSizes[i];
            String message = generateTestMessage(size);
            
            String testName = "AES Encrypt " + String(size) + "B";
            
            startBenchmark(testName.c_str());
            String encrypted = nostr::encryptData(testData.key, testData.iv, message);
            bool success = encrypted.length() > 0;
            endBenchmark(testName.c_str(), size, success);
            
            delay(100);
        }
    }

    void benchmarkNIP44Operations() {
        Serial.println("\n--- NIP-44 Encrypt/Decrypt Benchmark ---");
        
        size_t testSizes[] = {32, 128, 512, 2048, 4096};
        int numSizes = sizeof(testSizes) / sizeof(testSizes[0]);

        for (int i = 0; i < numSizes; i++) {
            size_t size = testSizes[i];
            String message = generateTestMessage(size);
            
            // Test NIP-44 Encryption
            String testName = "NIP44 Encrypt " + String(size) + "B";
            startBenchmark(testName.c_str());
            String encrypted = executeEncryptMessageNip44(message, testData.privateKeyHex, testData.publicKeyHex);
            bool encSuccess = encrypted.length() > 0;
            endBenchmark(testName.c_str(), size, encSuccess);
            
            if (encSuccess) {
                // Test NIP-44 Decryption
                testName = "NIP44 Decrypt " + String(size) + "B";
                startBenchmark(testName.c_str());
                String decrypted = executeDecryptMessageNip44(encrypted, testData.privateKeyHex, testData.publicKeyHex);
                bool decSuccess = decrypted.length() > 0;
                endBenchmark(testName.c_str(), size, decSuccess);
                
                if (decSuccess && decrypted != message) {
                    Serial.println("  WARNING: Decrypt result mismatch!");
                }
            }
            
            delay(200);
        }
    }

    void benchmarkFullNIPFlow() {
        Serial.println("\n--- Full NIP Message Flow Benchmark ---");
        
        size_t messageSize = 1024;
        String message = generateTestMessage(messageSize);
        
        // Create serialized JSON for testing
        String serializedJson = R"([
            "EVENT",
            {
                "id": "test123",
                "pubkey": ")" + testData.publicKeyHex + R"(",
                "created_at": 1234567890,
                "kind": 4,
                "tags": [["p", ")" + testData.publicKeyHex + R"("]],
                "content": ")" + message + R"(",
                "sig": "testsignature"
            }
        ])";
        
        // Test NIP-04 decrypt
        startBenchmark("NIP04 Full Decrypt 1KB");
        String nip04Result = nostr::nip04Decrypt(testData.privateKeyHex.c_str(), serializedJson);
        bool nip04Success = nip04Result.length() > 0;
        endBenchmark("NIP04 Full Decrypt 1KB", messageSize, nip04Success);
        
        // Test NIP-44 decrypt  
        startBenchmark("NIP44 Full Decrypt 1KB");
        String nip44Result = nostr::nip44Decrypt(testData.privateKeyHex.c_str(), serializedJson);
        bool nip44Success = nip44Result.length() > 0;
        endBenchmark("NIP44 Full Decrypt 1KB", messageSize, nip44Success);
    }

    void benchmarkMemoryFragmentation() {
        Serial.println("\n--- Memory Fragmentation Test ---");
        
        size_t initialHeap = esp_get_free_heap_size();
        
        // Perform multiple encrypt/decrypt cycles
        String message = generateTestMessage(1024);
        
        startBenchmark("Memory Fragmentation 10x");
        for (int i = 0; i < 10; i++) {
            String encrypted = nostr::encryptData(testData.key, testData.iv, message);
            
            // Create temporary buffer for decryption test
            size_t encSize = encrypted.length() / 2; // hex to bytes
            byte* encBytes = (byte*)malloc(encSize);
            if (encBytes) {
                // Convert hex to bytes (simplified)
                for (size_t j = 0; j < encSize && j * 2 < encrypted.length(); j++) {
                    encBytes[j] = (byte)j; // Simplified for benchmark
                }
                
                String decrypted = nostr::decryptData(testData.key, testData.iv, encBytes, encSize);
                free(encBytes);
            }
            
            esp_task_wdt_reset();
        }
        
        size_t finalHeap = esp_get_free_heap_size();
        bool success = (initialHeap - finalHeap) < 1024; // Less than 1KB memory leak
        
        endBenchmark("Memory Fragmentation 10x", 10240, success);
        
        Serial.printf("  Heap before: %d, after: %d, delta: %d bytes\n", 
                     initialHeap, finalHeap, (int)(initialHeap - finalHeap));
    }

    void runAllBenchmarks() {
        Serial.println("Starting comprehensive encryption benchmark suite...\n");
        
        benchmarkAESEncryptData();
        benchmarkAESDecryptData();
        benchmarkNIP44Operations();
        benchmarkFullNIPFlow();
        benchmarkMemoryFragmentation();
        
        printSummary();
    }

    void printSummary() {
        Serial.println("\n=== BENCHMARK SUMMARY ===");
        Serial.println("Test Name                    | Size    | Time(ms) | Heap(B) | Throughput(B/s) | Success");
        Serial.println("----------------------------|---------|----------|---------|-----------------|--------");
        
        for (int i = 0; i < resultCount; i++) {
            BenchmarkResult& r = results[i];
            Serial.printf("%-28s | %7d | %8lu | %7d | %13.2f | %s\n",
                         r.testName, 
                         r.dataSize,
                         r.executionTimeMs,
                         (int)r.heapDelta,
                         r.throughputBytesPerSec,
                         r.success ? "PASS" : "FAIL");
        }
        
        Serial.println("\n=== ANALYSIS ===");
        
        // Find fastest and slowest operations
        double maxThroughput = 0;
        double minThroughput = 999999;
        const char* fastestTest = "";
        const char* slowestTest = "";
        
        for (int i = 0; i < resultCount; i++) {
            if (results[i].success && results[i].throughputBytesPerSec > 0) {
                if (results[i].throughputBytesPerSec > maxThroughput) {
                    maxThroughput = results[i].throughputBytesPerSec;
                    fastestTest = results[i].testName;
                }
                if (results[i].throughputBytesPerSec < minThroughput) {
                    minThroughput = results[i].throughputBytesPerSec;
                    slowestTest = results[i].testName;
                }
            }
        }
        
        Serial.printf("Fastest operation: %s (%.2f B/s)\n", fastestTest, maxThroughput);
        Serial.printf("Slowest operation: %s (%.2f B/s)\n", slowestTest, minThroughput);
        Serial.printf("Performance ratio: %.2fx\n", maxThroughput / minThroughput);
        
        Serial.printf("\nFinal heap: %d bytes\n", esp_get_free_heap_size());
    }
};

} // namespace BenchmarkEncryption

// Global instance
BenchmarkEncryption::EncryptionBenchmark benchmark;

void setup() {
    benchmark.setup();
}

void loop() {
    benchmark.runAllBenchmarks();
    
    Serial.println("\nBenchmark complete. Waiting 30 seconds before next run...");
    delay(30000);
}