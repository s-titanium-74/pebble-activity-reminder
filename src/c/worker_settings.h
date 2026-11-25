#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "../../worker_src/c/inactivity.h"

enum {
  WORKER_MONITORING_KEY = 2,
  WORKER_MONITORING_PAUSED_KEY = 3,
  WORKER_SESSION_KEY = 4,
  WORKER_REMINDER_KEY = 5,
  WORKER_SETTINGS_KEY = 6,
  WORKER_START_REQUESTED = 1,
  // Acknowledgments carry a Unix timestamp (low/high bits) and a session ID.
  WORKER_ACTIVITY_ACKNOWLEDGED = 2,
  WORKER_REMINDER_REQUESTED = 3,
  WORKER_ACTIVITY_ACKNOWLEDGED_RESULT = 4,
  WORKER_SETTINGS_CHANGED = 5,
  WORKER_MONITORING_ENABLED = 1,
  WORKER_SETTINGS_VERSION = 1,
  WORKER_MIN_INTERVAL_MINUTES = 2,
  WORKER_MAX_INTERVAL_MINUTES = 60,
  WORKER_DEFAULT_INTERVAL_MINUTES = 30,
  WORKER_MIN_INTERVAL_SECONDS = WORKER_MIN_INTERVAL_MINUTES * ACTIVITY_MINUTE_SECONDS,
  WORKER_MAX_INTERVAL_SECONDS = WORKER_MAX_INTERVAL_MINUTES * ACTIVITY_MINUTE_SECONDS,
  WORKER_REMINDER_LIFETIME_SECONDS = 30,
};

typedef enum {
  REMINDER_VIBRATION_SHORT,
  REMINDER_VIBRATION_DOUBLE,
  REMINDER_VIBRATION_LONG,
} ReminderVibration;

typedef struct {
  uint32_t version;
  uint32_t detection_generation;
  int32_t interval_minutes;
  int32_t window_minutes;
  int32_t step_threshold;
  int32_t vibration;
} WorkerSettings;

typedef enum {
  SETTINGS_VALID,
  SETTINGS_INVALID_INTERVAL,
  SETTINGS_INVALID_WINDOW,
  SETTINGS_INVALID_STEPS,
  SETTINGS_INVALID_VIBRATION,
} WorkerSettingsValidation;

static inline WorkerSettingsValidation worker_validate_settings(const WorkerSettings *settings) {
  if (settings->interval_minutes < WORKER_MIN_INTERVAL_MINUTES ||
      settings->interval_minutes > WORKER_MAX_INTERVAL_MINUTES) {
    return SETTINGS_INVALID_INTERVAL;
  }
  if (settings->window_minutes < 1 || settings->window_minutes > settings->interval_minutes) {
    return SETTINGS_INVALID_WINDOW;
  }
  if (settings->step_threshold < 1 || settings->step_threshold > settings->window_minutes * UINT8_MAX) {
    return SETTINGS_INVALID_STEPS;
  }
  if (settings->vibration < REMINDER_VIBRATION_SHORT || settings->vibration > REMINDER_VIBRATION_LONG) {
    return SETTINGS_INVALID_VIBRATION;
  }
  return SETTINGS_VALID;
}

static inline bool worker_detection_changed(const WorkerSettings *before, const WorkerSettings *after) {
  return before->interval_minutes != after->interval_minutes ||
         before->window_minutes != after->window_minutes ||
         before->step_threshold != after->step_threshold;
}

typedef struct {
  uint32_t issued_at;
  uint32_t detection_generation;
  uint32_t last_activity_at;  // Zero means no activity has been recorded.
  uint16_t session;
} WorkerReminder;

int32_t worker_acknowledge_activity(void);
int32_t worker_stop(void);
bool worker_pending_reminder(void);
bool worker_controls_subscribe(void);
