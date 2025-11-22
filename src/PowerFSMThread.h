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

        // Check if we should schedule deferred radio-only sleep (after initial NodeInfo)
#ifndef ARCH_ESP32
        static bool g_radioOnlySleepScheduledInThread = false;
        static uint32_t lastRadioSleepDiagLog = 0;
        uint32_t nowMs = millis();

        // Periodic diagnostic logging so we can see condition values (every ~1s until scheduled)
        if (!g_radioOnlySleepScheduledInThread && (int32_t)(nowMs - lastRadioSleepDiagLog) > 1000) {
            LOG_DEBUG("RadioSleepCheck initSent=%d firstMs=%u now=%u active=%d scheduled=%d", g_nodeInfoInitialSent ? 1 : 0,
                      g_nodeInfoFirstSendMs, nowMs, g_radioOnlySleepActive ? 1 : 0,
                      g_radioOnlySleepScheduledInThread ? 1 : 0);
            lastRadioSleepDiagLog = nowMs;
        }
        
        if (!g_radioOnlySleepScheduledInThread && !g_radioOnlySleepActive && 
            g_nodeInfoInitialSent && g_nodeInfoFirstSendMs > 0) {
            // Wait 1.5s after NodeInfo was queued to allow transmission to complete
            if ((int32_t)(millis() - g_nodeInfoFirstSendMs) > 1500) {
                LOG_INFO("PowerFSMThread: Starting deferred radio-only sleep after initial NodeInfo TX");
                PowerFSM_enterRadioOnlySleep(Default::getConfiguredOrDefaultMs(config.power.sds_secs));
                g_radioOnlySleepScheduledInThread = true;
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