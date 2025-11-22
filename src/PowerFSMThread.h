#include "Default.h"
#include "NodeDB.h"
#include "PowerFSM.h"
#include "concurrency/OSThread.h"
#include "configuration.h"
#include "main.h"
#include "power.h"

// Forward declarations at global scope (used on non-ESP32 platforms)
extern bool g_radioOnlySleepActive;
extern bool g_nodeInfoInitialSent;
extern uint32_t g_nodeInfoFirstSendMs;
extern uint32_t g_lastRadioWakeMs;

namespace concurrency
{
/// Wrapper to convert our powerFSM stuff into a 'thread'
class PowerFSMThread : public OSThread
{
  public:
    // callback returns the period for the next callback invocation (or 0 if we should no longer be called)
    PowerFSMThread() : OSThread("PowerFSM") {}

  protected:
    int32_t runOnce() override
    {
#if !MESHTASTIC_EXCLUDE_POWER_FSM
        powerFSM.run_machine();

        // Ensure radio-only sleep expiration is serviced frequently
        PowerFSM_serviceRadioOnlySleep();

        // Check if we should schedule radio-only sleep cycles
#ifndef ARCH_ESP32
        static uint32_t lastRadioSleepDiagLog = 0;
        uint32_t nowMs = millis();

        // Periodic diagnostic logging (every ~1s when not sleeping)
        if (!g_radioOnlySleepActive && (int32_t)(nowMs - lastRadioSleepDiagLog) > 1000) {
            LOG_DEBUG("RadioSleepCheck initSent=%d firstMs=%u now=%u active=%d lastWake=%u", g_nodeInfoInitialSent ? 1 : 0,
                      g_nodeInfoFirstSendMs, nowMs, g_radioOnlySleepActive ? 1 : 0, g_lastRadioWakeMs);
            lastRadioSleepDiagLog = nowMs;
        }
        
        // Start radio-only sleep when:
        // 1. Not currently sleeping
        // 2. Initial NodeInfo has been sent (for first cycle only)
        // 3. Sufficient time has passed since last wake (or this is the first cycle)
        if (!g_radioOnlySleepActive) {
            bool firstCycle = (g_lastRadioWakeMs == 0);
            bool canStartSleep = false;
            
            if (firstCycle) {
                // First cycle: wait for NodeInfo + 1.5s
                if (g_nodeInfoInitialSent && g_nodeInfoFirstSendMs > 0 && 
                    (int32_t)(nowMs - g_nodeInfoFirstSendMs) > 1500) {
                    LOG_INFO("PowerFSMThread: Starting first radio-only sleep after initial NodeInfo TX");
                    canStartSleep = true;
                }
            } else {
                // Subsequent cycles: wait min_wake_secs after last wake
                uint32_t minWakeMs = Default::getConfiguredOrDefaultMs(config.power.min_wake_secs, default_min_wake_secs);
                if ((int32_t)(nowMs - g_lastRadioWakeMs) > (int32_t)minWakeMs) {
                    LOG_INFO("PowerFSMThread: Starting radio-only sleep cycle (awake for %u ms)", nowMs - g_lastRadioWakeMs);
                    canStartSleep = true;
                }
            }
            
            if (canStartSleep) {
                PowerFSM_enterRadioOnlySleep(Default::getConfiguredOrDefaultMs(config.power.sds_secs));
            }
        }
#endif

        /// If we are in power state we force the CPU to wake every 10ms to check for serial characters (we don't yet wake
        /// cpu for serial rx - FIXME)
        const State *state = powerFSM.getState();
        canSleep = (state != &statePOWER) && (state != &stateSERIAL);

        if (powerStatus->getHasUSB()) {
            timeLastPowered = millis();
        } else if (config.power.on_battery_shutdown_after_secs > 0 && config.power.on_battery_shutdown_after_secs != UINT32_MAX &&
                   millis() > (timeLastPowered +
                               Default::getConfiguredOrDefaultMs(
                                   config.power.on_battery_shutdown_after_secs))) { // shutdown after 30 minutes unpowered
            powerFSM.trigger(EVENT_SHUTDOWN);
        }

        return 100;
#else
        return INT32_MAX;
#endif
    }
};

} // namespace concurrency