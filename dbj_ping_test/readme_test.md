/*
 * README_TEST.md - Documentation for the test project
 * Place this file in the dbj_ping_test project directory
 */


# dbj_ping Test Application

This test application demonstrates the usage of the dbj_ping DLL and implements proper SEH (Structured Exception Handling) with minidump creation.

## Features

### Minidump Creation
- Automatic minidump creation on unhandled exceptions
- Manual minidump creation for testing
- Minidumps saved to `minidumps\` subdirectory
- Timestamped filenames for easy organization

### SEH Pattern Implementation
- Uses `__try`/`__finally` in all non-main functions
- Only uses `__except` in `main()` for top-level exception handling
- Proper resource cleanup in `__finally` blocks
- Returns int result pattern with `__leave` for early exits

### Interactive Testing
- Real-time ping monitoring
- Statistics display every 10 pings
- Keyboard commands for control:
  - 'q' - Quit
  - 's' - Show statistics
  - 'c' - Force countermeasures
  - 'r' - Reset statistics
  - 'h' - Help
  - 'p' - Pause/Resume

### Basic Functionality Tests
- DLL initialization verification
- Single ping execution test
- Statistics and configuration retrieval tests
- Error handling validation

## Usage

```bash
# Test with default target (8.8.8.8 or configured target)
dbj_ping_test.exe

# Test with specific target
dbj_ping_test.exe google.com

# Test with IP address
dbj_ping_test.exe 1.1.1.1
```

## Build Requirements

- Visual Studio 2019 or later
- Windows SDK 10.0 or later
- C17 standard support
- dbj_ping.dll must be built first

## Directory Structure

```
dbj_ping_test\
├── dbj_ping_test.c        # Main test application
├── minidump_writer.c      # Minidump creation implementation
├── minidump_writer.h      # Minidump header
├── README_TEST.md         # This file
└── minidumps\            # Created automatically for crash dumps
```

## Error Handling

The application implements a comprehensive error handling strategy:

1. **Function Level**: All functions use `__try`/`__finally` with int result pattern
2. **Resource Management**: Automatic cleanup in `__finally` blocks
3. **Exception Recovery**: Top-level `__except` handler in main with minidump creation
4. **Graceful Degradation**: Continues operation when possible, reports errors clearly

## Minidump Analysis

When crashes occur, minidumps are created in the `minidumps\` directory. These can be analyzed using:

- Visual Studio debugger
- WinDbg
- Other debugging tools that support Windows minidumps

The minidumps include:
- Process and thread information
- Call stacks
- Local variables
- Memory contents around the crash
- Module information

## Testing Scenarios

The application can test various scenarios:

1. **Normal Operation**: Basic ping functionality
2. **Network Issues**: High latency, packet loss simulation
3. **DNS Problems**: Invalid hostnames, DNS resolution failures
4. **Countermeasures**: Forced activation and monitoring
5. **Error Conditions**: Invalid parameters, initialization failures
6. **Exception Handling**: Crash simulation and recovery

## Configuration

The test application uses the same INI file as the DLL (`dbj_ping.ini`). Modify the configuration to test different scenarios:

- Adjust thresholds to trigger countermeasures more easily
- Change timeout values to simulate network conditions
- Enable/disable specific countermeasure types
- Modify DNS server lists for testing DNS switching