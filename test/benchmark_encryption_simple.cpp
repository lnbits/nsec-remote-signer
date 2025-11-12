#include <Arduino.h>
#include <esp_timer.h>
#include <esp_heap_caps.h>
#include <esp_task_wdt.h>

// Include AES library directly
extern "C" {
#include "../lib/aes/aes.h"
}

namespace SimpleBenchmark {

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

class AESBenchmark {
private:
    static const int MAX_RESULTS = 20;
    BenchmarkResult results[MAX_RESULTS];
    int resultCount = 0;
    unsigned long benchmarkStart;
    size_t heapBefore;

    // Test key and IV
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

        Serial.printf("  %lu ms, %d bytes used, %.2f B/s\n", 
                     duration, (int)heapDelta, throughput);
    }

public:
    void setup() {
        Serial.begin(115200);
        delay(2000);
        Serial.println("\n=== AES Performance Benchmark ===");
        Serial.printf("Free heap: %d bytes\n", esp_get_free_heap_size());
        Serial.printf("CPU frequency: %d MHz\n", getCpuFrequencyMhz());
    }

    void benchmarkAESEncryption() {
        Serial.println("\n--- AES-256-CBC Encryption ---");
        
        size_t testSizes[] = {16, 64, 256, 1024, 4096};
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
            
            String testName = "AES Encrypt " + String(size) + "B";
            
            startBenchmark(testName.c_str());
            
            // Perform AES encryption
            AES_ctx ctx;
            AES_init_ctx_iv(&ctx, testKey, testIV);
            AES_CBC_encrypt_buffer(&ctx, testData, paddedSize);
            
            bool success = true; // If we get here, it worked
            endBenchmark(testName.c_str(), size, success);
            
            free(testData);
            delay(50);
        }
    }

    void benchmarkAESDecryption() {
        Serial.println("\n--- AES-256-CBC Decryption ---");
        
        size_t testSizes[] = {16, 64, 256, 1024, 4096};
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
            
            String testName = "AES Decrypt " + String(size) + "B";
            
            startBenchmark(testName.c_str());
            
            // Perform AES decryption
            AES_ctx decCtx;
            AES_init_ctx_iv(&decCtx, testKey, testIV);
            AES_CBC_decrypt_buffer(&decCtx, testData, paddedSize);
            
            bool success = true; // If we get here, it worked
            endBenchmark(testName.c_str(), size, success);
            
            free(testData);
            delay(50);
        }
    }

    void benchmarkMultipleOperations() {
        Serial.println("\n--- Multiple Operations Test ---");
        
        size_t dataSize = 1024;
        size_t paddedSize = ((dataSize + 15) / 16) * 16;
        int iterations = 10;
        
        startBenchmark("AES 10x Encrypt/Decrypt");
        
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
        
        endBenchmark("AES 10x Encrypt/Decrypt", totalProcessed, allSuccess);
    }

    void runAllBenchmarks() {
        Serial.println("Starting AES benchmark suite...\n");
        
        benchmarkAESEncryption();
        benchmarkAESDecryption();
        benchmarkMultipleOperations();
        
        printSummary();
    }

    void printSummary() {
        Serial.println("\n=== BENCHMARK SUMMARY ===");
        Serial.println("Test Name                | Size    | Time(ms) | Heap(B) | Throughput(B/s)");
        Serial.println("-------------------------|---------|----------|---------|----------------");
        
        double totalThroughput = 0;
        int validResults = 0;
        
        for (int i = 0; i < resultCount; i++) {
            BenchmarkResult& r = results[i];
            Serial.printf("%-24s | %7d | %8lu | %7d | %12.2f\n",
                         r.testName, 
                         r.dataSize,
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