# GET-165 security correction

Malformed FAT12 cluster numbers could index beyond the cached FAT (Pico) or outside its SRAM region (Nano). Reject them before memory/sector access. The real parser is tested with adversarial cluster 65535, rejected writes, and an invalid file chain. Normal existing tests remain enabled.

The new test compiles the actual vendored FAT12 implementation, which the older test suite replaced with a mock. This applies the matching upstream 64korppu guard locally.

Verification: focused AddressSanitizer/UndefinedBehaviorSanitizer checks and the existing native make test command. No firmware was flashed and no physical drive was accessed. Device compiler, electrical behavior and hardware integration remain outside these host checks.
