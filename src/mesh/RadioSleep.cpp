#include <stdint.h>
// Dedicated translation unit for radio-only sleep globals
// These symbols are referenced from multiple modules (PowerFSM, RadioLibInterface, SX126xInterface, RF95Interface)
// to avoid linker undefined references if PowerFSM is conditionally excluded.

bool g_radioOnlySleepActive = false;
uint32_t g_radioOnlySleepEndMs = 0;
uint32_t g_lastRadioWakeMs = 0;  // Timestamp when radio-only sleep last ended
