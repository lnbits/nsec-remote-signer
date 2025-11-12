# Nostr Encryption Benchmarking Suite

This benchmark suite measures the performance of encryption and decryption functions in the Nostr Remote Signer device to identify optimization opportunities.

## Quick Start

### Running the Benchmark

```bash
# Build and upload benchmark tests to ESP32
pio run --environment benchmark --target upload

# Monitor results
pio device monitor --environment benchmark
```

### Build Commands

```bash
# Build benchmark only
pio run --environment benchmark

# Clean and rebuild
pio run --environment benchmark --target clean
pio run --environment benchmark

# Upload and monitor in one command
pio run --environment benchmark --target upload && pio device monitor --environment benchmark
```

## Test Coverage

### Core Functions Tested
- `nostr::decryptData()` - AES-256-CBC decryption (lib/nostr/nostr.cpp:46)
- `nostr::encryptData()` - AES-256-CBC encryption (lib/nostr/nostr.cpp:345)
- `executeEncryptMessageNip44()` - NIP-44 encryption with ChaCha20
- `executeDecryptMessageNip44()` - NIP-44 decryption with ChaCha20

### Test Categories

1. **AES Raw Performance** - Direct encryption/decryption of various message sizes
2. **NIP-44 Operations** - Full NIP-44 encrypt/decrypt cycle with padding
3. **Full NIP Flow** - Complete message processing (JSON parsing + crypto)
4. **Memory Fragmentation** - Multiple operations to detect memory leaks

### Test Data Sizes
- Small: 16-64 bytes
- Medium: 256-1024 bytes  
- Large: 4096-8192 bytes

## Metrics Collected

### Performance Metrics
- **Execution Time** (milliseconds) - Total time for operation
- **Throughput** (bytes/second) - Data processed per second
- **Memory Usage** (bytes) - Heap allocated during operation
- **Memory Fragmentation** - Heap changes over multiple operations

### Output Format
```
Test Name                    | Size    | Time(ms) | Heap(B) | Throughput(B/s) | Success
----------------------------|---------|----------|---------|-----------------|--------
AES Encrypt 1024B           |    1024 |       45 |     128 |        22755.56 | PASS
AES Decrypt 1024B           |    1024 |       38 |      64 |        26947.37 | PASS
NIP44 Encrypt 1024B         |    1024 |      156 |     512 |         6564.10 | PASS
```

## Interpreting Results

### Performance Indicators

**Good Performance:**
- AES operations: >10,000 B/s
- NIP-44 operations: >3,000 B/s
- Memory usage: <500 bytes per operation
- No memory fragmentation over time

**Performance Issues:**
- Throughput <1,000 B/s indicates bottlenecks
- Memory usage >1KB per operation suggests inefficiency
- Heap fragmentation >100 bytes after 10 operations

### Common Bottlenecks

1. **Memory Allocation** - Frequent malloc/free calls
2. **String Operations** - Arduino String concatenation
3. **Base64 Conversion** - Encoding/decoding overhead
4. **ECDH Key Derivation** - Expensive elliptic curve operations

## Optimization Targets

Based on benchmark results, focus optimization on:

### High Impact Areas
1. **Memory Management** - Replace malloc with pre-allocated pools
2. **String Operations** - Use char arrays instead of Arduino String
3. **Hardware Acceleration** - Enable ESP32 AES hardware support

### Medium Impact Areas
1. **Algorithm Selection** - Compare AES vs ChaCha20 performance
2. **Buffer Sizes** - Optimize buffer allocation strategies
3. **Padding Efficiency** - Minimize padding overhead

## Development Workflow

### 1. Baseline Measurement
```bash
# Run initial benchmark to establish baseline
pio run --environment benchmark --target upload
pio device monitor --environment benchmark
# Save results as baseline_results.txt
```

### 2. Make Optimizations
- Edit functions in `lib/nostr/nostr.cpp` or `lib/nostr/nip44/`
- Test individual changes incrementally

### 3. Measure Improvement
```bash
# Test optimization impact
pio run --environment benchmark --target upload
pio device monitor --environment benchmark
# Compare with baseline
```

### 4. Regression Testing
```bash
# Ensure main firmware still works
pio run --environment esp32-s3-n16r8v --target upload
pio device monitor --environment esp32-s3-n16r8v
```

## Configuration

### Benchmark Settings
- **Execution Environment**: ESP32-S3 with 16MB Flash, 8MB PSRAM
- **Optimization Level**: -O2 (set in platformio.ini)
- **Memory Pool**: 8KB JSON + 16KB encryption buffer
- **Watchdog**: Reset every test to prevent timeouts

### Modifying Tests

Edit `test/benchmark_encryption.cpp` to:
- Add new test cases
- Modify test data sizes
- Change test iterations
- Add custom performance metrics

### Test Data
- **Private Key**: Deterministic test key for reproducible results
- **Message Content**: Repeating pattern for consistent data
- **Encryption Keys**: Fixed test vectors for baseline comparison

## Example Results Analysis

### Sample Output
```
=== BENCHMARK SUMMARY ===
Fastest operation: AES Encrypt 64B (28571.43 B/s)
Slowest operation: NIP44 Decrypt 4096B (1843.24 B/s)
Performance ratio: 15.50x
Final heap: 145628 bytes
```

### Performance Interpretation
- **15x performance difference** suggests significant optimization potential in NIP-44
- **Final heap close to initial** indicates good memory management
- **Large message penalty** shows scaling issues need attention

## Troubleshooting

### Common Issues

**Build Errors:**
```bash
# Missing dependencies
pio lib install

# Clean build
pio run --environment benchmark --target clean
pio run --environment benchmark
```

**Upload Issues:**
```bash
# Check device connection
pio device list

# Manual reset and try again
pio run --environment benchmark --target upload --upload-port /dev/cu.usbmodem*
```

**Memory Errors:**
- Increase heap size in test if operations fail
- Reduce test data sizes for memory-constrained operations
- Check for memory leaks in fragmentation tests

### Serial Output Issues
- Ensure monitor_speed = 115200 in platformio.ini
- Use `pio device monitor --baud 115200` if needed
- Check for buffer overflow in large message tests

---

**Next Steps:** Use benchmark results to prioritize optimization efforts and measure improvement impact systematically.