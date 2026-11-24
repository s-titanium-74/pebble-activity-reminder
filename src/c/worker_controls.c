#include <pebble.h>

#include "worker_settings.h"

static bool s_reminder_available = true;
static uint32_t s_acknowledged_at;
static uint16_t s_acknowledgment_session;
static bool s_acknowledgment_received;

typedef enum {
  REMINDER_VIBRATION_SHORT,
} ReminderVibration;

static void vibrate_reminder(ReminderVibration pattern) {
  switch (pattern) {
    case REMINDER_VIBRATION_SHORT:
      vibes_short_pulse();
      break;
  }
}

static uint16_t current_session(void) {
  return persist_read_int(WORKER_SESSION_KEY);
}

static int32_t write_setting(int key, int32_t value) {
  const int result = persist_write_int(key, value);
  return result == sizeof(value) ? 0 : result < 0 ? result : E_INTERNAL;
}

int32_t worker_active_interval(void) {
  return app_worker_is_running() && !persist_read_bool(WORKER_MONITORING_PAUSED_KEY)
      ? persist_read_int(WORKER_INTERVAL_KEY) : 0;
}

int32_t worker_monitoring_paused(void) {
  return persist_read_int(WORKER_INTERVAL_KEY) >= WORKER_MIN_INTERVAL_SECONDS &&
         persist_read_bool(WORKER_MONITORING_PAUSED_KEY);
}

int32_t worker_is_running(void) {
  return app_worker_is_running();
}

int32_t worker_default_interval(void) {
  return WORKER_DEFAULT_INTERVAL_SECONDS;
}

int32_t worker_start(int32_t seconds) {
  if (seconds != WORKER_DIAGNOSTIC_INTERVAL_SECONDS &&
      (seconds < WORKER_MIN_INTERVAL_SECONDS || seconds > WORKER_MAX_INTERVAL_SECONDS)) {
    return E_INVALID_ARGUMENT;
  }

  const bool running = app_worker_is_running();
  const int32_t previous_interval = worker_active_interval();
  if (!previous_interval ||
      ((seconds == WORKER_DIAGNOSTIC_INTERVAL_SECONDS) !=
       (previous_interval == WORKER_DIAGNOSTIC_INTERVAL_SECONDS))) {
    const uint16_t session = current_session() + 1;
    const int32_t result = write_setting(WORKER_SESSION_KEY, session);
    if (result) {
      return result;
    }
  }

  if (!running && persist_read_bool(WORKER_MONITORING_PAUSED_KEY)) {
    const int result = persist_delete(WORKER_MONITORING_PAUSED_KEY);
    if (result < 0) {
      return result;
    }
  }
  const int32_t result = write_setting(WORKER_INTERVAL_KEY, seconds);
  if (result) {
    return result;
  }

  // An earlier comparison run could otherwise interrupt the foreground app.
  wakeup_cancel_all();
  AppWorkerResult launch_result = running
      ? APP_WORKER_RESULT_ALREADY_RUNNING
      : app_worker_launch();
  if (launch_result == APP_WORKER_RESULT_ALREADY_RUNNING) {
    AppWorkerMessage message = {0};
    app_worker_send_message(WORKER_INTERVAL_CHANGED, &message);
    return APP_WORKER_RESULT_SUCCESS;
  }
  return launch_result;
}

int32_t worker_acknowledge_activity(void) {
  if (worker_active_interval() < WORKER_MIN_INTERVAL_SECONDS) {
    return APP_WORKER_RESULT_NOT_RUNNING;
  }
  s_acknowledged_at = time(NULL);
  s_acknowledgment_session = current_session();
  s_acknowledgment_received = false;
  AppWorkerMessage message = {
    .data0 = (uint16_t)s_acknowledged_at,
    .data1 = s_acknowledged_at >> 16,
    .data2 = s_acknowledgment_session,
  };
  app_worker_send_message(WORKER_ACTIVITY_ACKNOWLEDGED, &message);
  return APP_WORKER_RESULT_SUCCESS;
}

int32_t worker_acknowledgment_received(void) {
  return s_acknowledgment_received && current_session() == s_acknowledgment_session;
}

int32_t worker_stop(void) {
  // Invalidate queued reminders before asking the OS to stop the worker.
  const int32_t result = write_setting(WORKER_INTERVAL_KEY, 0);
  if (result) {
    return result;
  }
  const AppWorkerResult stopped = app_worker_is_running()
      ? app_worker_kill() : APP_WORKER_RESULT_NOT_RUNNING;
  return stopped == APP_WORKER_RESULT_NOT_RUNNING ? APP_WORKER_RESULT_SUCCESS : stopped;
}

static bool read_pending_reminder(WorkerReminder *reminder) {
  if (worker_active_interval() < WORKER_MIN_INTERVAL_SECONDS ||
      persist_read_data(WORKER_REMINDER_KEY, reminder, sizeof(*reminder)) != sizeof(*reminder)) {
    return false;
  }
  const uint32_t now = time(NULL);
  return reminder->session == current_session() && reminder->issued_at <= now &&
         now - reminder->issued_at < WORKER_REMINDER_LIFETIME_SECONDS;
}

bool worker_pending_reminder(void) {
  WorkerReminder reminder;
  return read_pending_reminder(&reminder);
}

uint32_t worker_take_reminder(void) {
  if (!s_reminder_available) {
    return UINT32_MAX;
  }
  s_reminder_available = false;
  WorkerReminder reminder;
  if (!read_pending_reminder(&reminder) || persist_delete(WORKER_REMINDER_KEY) < 0 ||
      worker_active_interval() < WORKER_MIN_INTERVAL_SECONDS || reminder.session != current_session()) {
    return UINT32_MAX;
  }
  vibrate_reminder(REMINDER_VIBRATION_SHORT);
  APP_LOG(APP_LOG_LEVEL_INFO, "Reminder displayed issued_at=%lu last_activity=%lu",
          (unsigned long)reminder.issued_at, (unsigned long)reminder.last_activity_at);
  return reminder.last_activity_at;
}

uint32_t worker_elapsed_activity_minutes(uint32_t timestamp) {
  const uint32_t now = time(NULL);
  return timestamp <= now ? (now - timestamp) / 60 : 0;
}

static void on_worker_message(uint16_t type, AppWorkerMessage *message) {
  if (type == WORKER_REMINDER_REQUESTED) {
    s_reminder_available = true;
  } else if (type == WORKER_ACTIVITY_ACKNOWLEDGED_RESULT) {
    const uint32_t acknowledged_at = ((uint32_t)message->data1 << 16) | message->data0;
    if (acknowledged_at == s_acknowledged_at && message->data2 == s_acknowledgment_session) {
      s_acknowledgment_received = true;
    }
  }
}

bool worker_controls_subscribe(void) {
  return app_worker_message_subscribe(on_worker_message);
}
