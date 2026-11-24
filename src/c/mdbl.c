#include <pebble.h>
#include "worker_settings.h"

void handle_spike_wakeup(void);

int main(void) {
  AppLaunchReason reason = launch_reason();
  if (reason == APP_LAUNCH_WAKEUP) {
    // Alloy's event loop exits before JavaScript starts if there is no window.
    handle_spike_wakeup();
    return 0;
  }
  if (reason == APP_LAUNCH_WORKER) {
    if (!worker_pending_reminder()) {
      return 0;
    }
  } else if (reason != APP_LAUNCH_USER && reason != APP_LAUNCH_QUICK_LAUNCH) {
    return 0;
  }

  Window *window = window_create();
  if (!window) {
    APP_LOG(APP_LOG_LEVEL_ERROR, "Activity Reminder could not open its window");
    return 0;
  }
  window_stack_push(window, false);
  ModdableCreationRecord creation = {
    .recordSize = sizeof(creation),
    .fxBuildFFI = fxBuildFFI,
#ifdef PBL_DEBUG
    .flags = kModdableCreationFlagDebug,
#endif
  };
  const bool subscribed = worker_controls_subscribe();
  if (!subscribed) {
    APP_LOG(APP_LOG_LEVEL_ERROR, "Activity Reminder could not subscribe to its worker");
  }
  moddable_createMachine(&creation);
  if (subscribed) {
    app_worker_message_unsubscribe();
  }
  window_destroy(window);
  return 0;
}

int32_t worker_launched_reminder(void) {
  return launch_reason() == APP_LAUNCH_WORKER;
}
