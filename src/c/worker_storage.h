#pragma once

#include "worker_settings.h"

static inline WorkerSettings worker_read_settings(void) {
  WorkerSettings settings;
  if (persist_get_size(WORKER_SETTINGS_KEY) == sizeof(settings) &&
      persist_read_data(WORKER_SETTINGS_KEY, &settings, sizeof(settings)) == sizeof(settings) &&
      settings.version == WORKER_SETTINGS_VERSION && worker_validate_settings(&settings) == SETTINGS_VALID) {
    return settings;
  }

  return (WorkerSettings){
    .version = WORKER_SETTINGS_VERSION,
    .interval_minutes = WORKER_DEFAULT_INTERVAL_MINUTES,
    .window_minutes = ACTIVITY_DEFAULT_WINDOW,
    .step_threshold = ACTIVITY_DEFAULT_STEPS,
    .vibration = REMINDER_VIBRATION_SHORT,
  };
}

static inline bool worker_monitoring_enabled(void) {
  const int32_t state = persist_read_int(WORKER_MONITORING_KEY);
  // Earlier builds stored the active interval here. Ten seconds was diagnostics.
  return state == WORKER_MONITORING_ENABLED ||
         (state >= WORKER_MIN_INTERVAL_SECONDS && state <= WORKER_MAX_INTERVAL_SECONDS);
}

static inline uint16_t worker_current_session(void) {
  return persist_read_int(WORKER_SESSION_KEY);
}
