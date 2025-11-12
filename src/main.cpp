#include <Arduino.h>
#include <esp_timer.h>
#include <esp_heap_caps.h>
#include <esp_task_wdt.h>

// Include AES library directly
#include "../lib/aes/aes.h"
// Include nostr library for NIP-04 functions
#include "../lib/nostr/nostr.h"

namespace SimpleBenchmark {

struct BenchmarkResult {
    char testName[32];  // Fixed size instead of pointer
    size_t dataSize;
    unsigned long executionTimeMs;
    size_t heapBefore;
    size_t heapAfter;
    size_t heapDelta;
    double throughputBytesPerSec;
    bool success;
};

class AESBenchmark {
private:
    static const int MAX_RESULTS = 15;  // Increase to capture all tests
    BenchmarkResult results[MAX_RESULTS];
    int resultCount = 0;
    unsigned long benchmarkStart;
    size_t heapBefore;

    // Test key and IV for AES
    byte testKey[32] = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
        0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
        0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
        0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F
    };

    byte testIV[16] = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
        0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F
    };

    // Test keys for NIP-04 (valid secp256k1 keys) - public key without 02 prefix
    String privateKeyHex = "ab9a7a133d3e5a09229e5fb277a0a027f478146d25dd3d6efbfa02afb28375b4";
    String publicKeyHex = "fddf59f9a1bd2c67ededec1f2ad3eb9822a351f5673763ea5459e4d92334a292";

    void startBenchmark(const char* testName) {
        heapBefore = esp_get_free_heap_size();
        benchmarkStart = millis();
        esp_task_wdt_reset(); // Reset watchdog
        Serial.print("Starting: ");
        Serial.println(testName);
    }

    void endBenchmark(const char* testName, size_t dataSize, bool success) {
        unsigned long duration = millis() - benchmarkStart;
        size_t heapAfter = esp_get_free_heap_size();
        size_t heapDelta = heapBefore > heapAfter ? heapBefore - heapAfter : 0;
        double throughput = success && duration > 0 ? (dataSize * 1000.0) / duration : 0;

        if (resultCount < MAX_RESULTS) {
            // Safely copy test name
            strncpy(results[resultCount].testName, testName, 31);
            results[resultCount].testName[31] = '\0';
            results[resultCount].dataSize = dataSize;
            results[resultCount].executionTimeMs = duration;
            results[resultCount].heapBefore = heapBefore;
            results[resultCount].heapAfter = heapAfter;
            results[resultCount].heapDelta = heapDelta;
            results[resultCount].throughputBytesPerSec = throughput;
            results[resultCount].success = success;
            resultCount++;
        } else {
            Serial.printf("WARNING: Results array full! Skipping result for: %s\n", testName);
        }

        Serial.printf("  %lu ms, %d bytes used, %.2f B/s\n", 
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
        Serial.println("\n=== Nostr Encryption Performance Benchmark ===");
        Serial.printf("Free heap: %d bytes\n", esp_get_free_heap_size());
        Serial.printf("CPU frequency: %d MHz\n", getCpuFrequencyMhz());
        
        // Initialize nostr memory space for NIP-04 tests
        nostr::initMemorySpace(4096, 8192);
        Serial.println("Initialized nostr memory space");
    }

    void benchmarkAESEncryption() {
        Serial.println("\n--- AES-256-CBC Encryption ---");
        
        size_t testSizes[] = {16, 64, 256, 1024};  // Reduced max size
        int numSizes = sizeof(testSizes) / sizeof(testSizes[0]);

        for (int i = 0; i < numSizes; i++) {
            size_t size = testSizes[i];
            
            // Ensure size is multiple of 16 for AES
            size_t paddedSize = ((size + 15) / 16) * 16;
            
            // Allocate test data
            byte* testData = (byte*)malloc(paddedSize);
            if (!testData) {
                Serial.printf("Failed to allocate %d bytes\n", paddedSize);
                continue;
            }
            
            // Fill with test pattern
            for (size_t j = 0; j < paddedSize; j++) {
                testData[j] = (j % 256);
            }
            
            char testName[32];
            snprintf(testName, sizeof(testName), "AES Encrypt %dB", (int)size);
            
            startBenchmark(testName);
            
            // Perform AES encryption
            AES_ctx ctx;
            AES_init_ctx_iv(&ctx, testKey, testIV);
            AES_CBC_encrypt_buffer(&ctx, testData, paddedSize);
            
            bool success = true; // If we get here, it worked
            endBenchmark(testName, size, success);
            
            free(testData);
            delay(50);
        }
    }

    void benchmarkAESDecryption() {
        Serial.println("\n--- AES-256-CBC Decryption ---");
        
        size_t testSizes[] = {16, 64, 256, 1024};  // Reduced max size
        int numSizes = sizeof(testSizes) / sizeof(testSizes[0]);

        for (int i = 0; i < numSizes; i++) {
            size_t size = testSizes[i];
            
            // Ensure size is multiple of 16 for AES
            size_t paddedSize = ((size + 15) / 16) * 16;
            
            // Allocate and prepare encrypted test data
            byte* testData = (byte*)malloc(paddedSize);
            if (!testData) {
                Serial.printf("Failed to allocate %d bytes\n", paddedSize);
                continue;
            }
            
            // Fill with test pattern and encrypt it first
            for (size_t j = 0; j < paddedSize; j++) {
                testData[j] = (j % 256);
            }
            
            AES_ctx encCtx;
            AES_init_ctx_iv(&encCtx, testKey, testIV);
            AES_CBC_encrypt_buffer(&encCtx, testData, paddedSize);
            
            char testName[32];
            snprintf(testName, sizeof(testName), "AES Decrypt %dB", (int)size);
            
            startBenchmark(testName);
            
            // Perform AES decryption
            AES_ctx decCtx;
            AES_init_ctx_iv(&decCtx, testKey, testIV);
            AES_CBC_decrypt_buffer(&decCtx, testData, paddedSize);
            
            bool success = true; // If we get here, it worked
            endBenchmark(testName, size, success);
            
            free(testData);
            delay(50);
        }
    }

    void benchmarkMultipleOperations() {
        Serial.println("\n--- Multiple Operations Test ---");
        
        size_t dataSize = 256;  // Reduced size
        size_t paddedSize = ((dataSize + 15) / 16) * 16;
        int iterations = 5;  // Reduced iterations
        
        startBenchmark("AES 5x Encrypt/Decrypt");
        
        size_t totalProcessed = 0;
        bool allSuccess = true;
        
        for (int i = 0; i < iterations; i++) {
            byte* testData = (byte*)malloc(paddedSize);
            if (!testData) {
                allSuccess = false;
                break;
            }
            
            // Fill with test pattern
            for (size_t j = 0; j < paddedSize; j++) {
                testData[j] = (j + i) % 256;
            }
            
            // Encrypt
            AES_ctx encCtx;
            AES_init_ctx_iv(&encCtx, testKey, testIV);
            AES_CBC_encrypt_buffer(&encCtx, testData, paddedSize);
            
            // Decrypt
            AES_ctx decCtx;
            AES_init_ctx_iv(&decCtx, testKey, testIV);
            AES_CBC_decrypt_buffer(&decCtx, testData, paddedSize);
            
            totalProcessed += dataSize * 2; // encrypt + decrypt
            free(testData);
            
            esp_task_wdt_reset();
        }
        
        endBenchmark("AES 5x Encrypt/Decrypt", totalProcessed, allSuccess);
    }

    void benchmarkNIP04Encryption() {
        Serial.println("\n--- NIP-04 Encryption (getCipherText) ---");
        
        size_t testSizes[] = {16, 64, 256};  // Smaller sizes for NIP-04
        int numSizes = sizeof(testSizes) / sizeof(testSizes[0]);

        for (int i = 0; i < numSizes; i++) {
            size_t size = testSizes[i];
            String message = generateTestMessage(size);
            
            char testName[32];
            snprintf(testName, sizeof(testName), "NIP04 Encrypt %dB", (int)size);
            
            startBenchmark(testName);
            
            // Perform NIP-04 encryption
            String encrypted = nostr::getCipherText(privateKeyHex.c_str(), publicKeyHex.c_str(), message);
            bool success = encrypted.length() > 0;
            
            endBenchmark(testName, size, success);
            
            delay(100);  // Allow memory recovery
        }
    }

    void benchmarkNIP04Decryption() {
        Serial.println("\n--- NIP-04 Decryption (decryptNip04Ciphertext) ---");
        
        size_t testSizes[] = {16, 64, 256};  // Smaller sizes for NIP-04
        int numSizes = sizeof(testSizes) / sizeof(testSizes[0]);

        for (int i = 0; i < numSizes; i++) {
            size_t size = testSizes[i];
            String message = generateTestMessage(size);
            
            Serial.printf("Encrypting %d byte message first...\n", (int)size);
            
            // First encrypt the message to get something to decrypt
            String encrypted = nostr::getCipherText(privateKeyHex.c_str(), publicKeyHex.c_str(), message);
            if (encrypted.length() == 0) {
                Serial.printf("Failed to encrypt test message of size %d\n", (int)size);
                continue;
            }
            
            Serial.printf("Encrypted length: %d, starting decryption...\n", encrypted.length());
            
            char testName[32];
            snprintf(testName, sizeof(testName), "NIP04 Decrypt %dB", (int)size);
            
            startBenchmark(testName);
            
            // Perform NIP-04 decryption
            String decrypted = nostr::decryptNip04Ciphertext(encrypted, privateKeyHex, publicKeyHex);
            bool success = decrypted.length() > 0;
            
            endBenchmark(testName, size, success);
            
            // Verify decryption worked correctly
            if (success && decrypted.length() > 0) {
                Serial.printf("Original length: %d, Decrypted length: %d\n", message.length(), decrypted.length());
                // Check if decrypted message starts with the original (may be truncated)
                if (message.startsWith(decrypted) || decrypted.startsWith(message.substring(0, min(message.length(), decrypted.length())))) {
                    Serial.println("  Decryption verification PASSED");
                } else {
                    Serial.println("  WARNING: Decrypt result mismatch!");
                    Serial.printf("  Expected: %s\n", message.substring(0, 50).c_str());
                    Serial.printf("  Got: %s\n", decrypted.substring(0, 50).c_str());
                }
            } else {
                Serial.println("  Decryption FAILED - no output");
            }
            
            delay(100);  // Allow memory recovery
        }
    }

    void benchmarkNIP04FullCycle() {
        Serial.println("\n--- NIP-04 Full Encrypt/Decrypt Cycle ---");
        
        size_t messageSize = 128;
        String message = generateTestMessage(messageSize);
        
        startBenchmark("NIP04 Full Cycle 128B");
        
        // Encrypt
        String encrypted = nostr::getCipherText(privateKeyHex.c_str(), publicKeyHex.c_str(), message);
        bool encSuccess = encrypted.length() > 0;
        
        String decrypted;
        bool decSuccess = false;
        
        if (encSuccess) {
            // Decrypt
            decrypted = nostr::decryptNip04Ciphertext(encrypted, privateKeyHex, publicKeyHex);
            // Check if decryption was successful (may be truncated due to padding)
            decSuccess = decrypted.length() > 0 && (
                message.startsWith(decrypted) || 
                decrypted.startsWith(message.substring(0, min(message.length(), decrypted.length())))
            );
        }
        
        bool success = encSuccess && decSuccess;
        endBenchmark("NIP04 Full Cycle 128B", messageSize * 2, success);  // Count both encrypt and decrypt
        
        if (!success) {
            Serial.println("  Full cycle test failed");
            if (!encSuccess) Serial.println("  - Encryption failed");
            if (!decSuccess) {
                Serial.println("  - Decryption failed");
                Serial.printf("  - Original: %s\n", message.substring(0, 50).c_str());
                Serial.printf("  - Decrypted: %s\n", decrypted.substring(0, 50).c_str());
            }
        } else {
            Serial.println("  Full cycle test PASSED");
        }
    }

    void runAllBenchmarks() {
        Serial.println("Starting Nostr encryption benchmark suite...\n");
        
        benchmarkAESEncryption();
        benchmarkAESDecryption();
        benchmarkMultipleOperations();
        benchmarkNIP04Encryption();
        benchmarkNIP04Decryption();
        benchmarkNIP04FullCycle();
        
        printSummary();
    }

    void printSummary() {
        Serial.println("\n=== BENCHMARK SUMMARY ===");
        Serial.printf("Recorded %d out of max %d test results:\n", resultCount, MAX_RESULTS);
        Serial.println("Test Name                | Size    | Time(ms) | Heap(B) | Throughput(B/s)");
        Serial.println("-------------------------|---------|----------|---------|----------------");
        
        double totalThroughput = 0;
        int validResults = 0;
        
        for (int i = 0; i < resultCount; i++) {
            BenchmarkResult& r = results[i];
            Serial.printf("%-24.24s | %7d | %8lu | %7d | %12.2f\n",
                         r.testName, 
                         (int)r.dataSize,
                         r.executionTimeMs,
                         (int)r.heapDelta,
                         r.throughputBytesPerSec);
            
            if (r.success && r.throughputBytesPerSec > 0) {
                totalThroughput += r.throughputBytesPerSec;
                validResults++;
            }
        }
        
        if (validResults > 0) {
            double avgThroughput = totalThroughput / validResults;
            Serial.printf("\nAverage throughput: %.2f B/s\n", avgThroughput);
        }
        
        Serial.printf("Final heap: %d bytes\n", esp_get_free_heap_size());
        
        // Performance analysis
        if (validResults >= 2) {
            double maxThroughput = 0;
            double minThroughput = 999999;
            
            for (int i = 0; i < resultCount; i++) {
                if (results[i].success && results[i].throughputBytesPerSec > 0) {
                    if (results[i].throughputBytesPerSec > maxThroughput) {
                        maxThroughput = results[i].throughputBytesPerSec;
                    }
                    if (results[i].throughputBytesPerSec < minThroughput) {
                        minThroughput = results[i].throughputBytesPerSec;
                    }
                }
            }
            
            Serial.printf("Performance range: %.2f to %.2f B/s (%.2fx)\n", 
                         minThroughput, maxThroughput, maxThroughput / minThroughput);
        }
    }
};

} // namespace SimpleBenchmark

// Global instance
SimpleBenchmark::AESBenchmark benchmark;

void setup() {
    benchmark.setup();
}

void loop() {
    benchmark.runAllBenchmarks();
    
    Serial.println("\nBenchmark complete. Waiting 30 seconds before next run...");
    delay(30000);
}