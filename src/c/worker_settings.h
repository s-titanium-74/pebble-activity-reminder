#pragma once

#include <stdbool.h>
#include <stdint.h>

enum {
  WORKER_INTERVAL_KEY = 2,
  WORKER_MONITORING_PAUSED_KEY = 3,
  WORKER_SESSION_KEY = 4,
  WORKER_REMINDER_KEY = 5,
  WORKER_INTERVAL_CHANGED = 1,
  // Acknowledgments carry a Unix timestamp (low/high bits) and a session ID.
  WORKER_ACTIVITY_ACKNOWLEDGED = 2,
  WORKER_REMINDER_REQUESTED = 3,
  WORKER_ACTIVITY_ACKNOWLEDGED_RESULT = 4,
  WORKER_DIAGNOSTIC_INTERVAL_SECONDS = 10,
  WORKER_MIN_INTERVAL_SECONDS = 120,
  WORKER_MAX_INTERVAL_SECONDS = 3600,
  WORKER_DEFAULT_INTERVAL_SECONDS = 1800,
  WORKER_REMINDER_LIFETIME_SECONDS = 30,
};

typedef struct {
  uint32_t issued_at;
  uint32_t last_activity_at;  // Zero means no activity has been recorded.
  uint16_t session;
} WorkerReminder;

int32_t worker_acknowledge_activity(void);
int32_t worker_stop(void);
bool worker_pending_reminder(void);
bool worker_controls_subscribe(void);
