#include <pebble.h>

#include "worker_storage.h"

static bool s_reminder_available = true;
static uint32_t s_acknowledged_at;
static uint16_t s_acknowledgment_session;
static bool s_acknowledgment_received;

static void vibrate_reminder(ReminderVibration pattern) {
  switch (pattern) {
    case REMINDER_VIBRATION_SHORT:
      vibes_short_pulse();
      break;
    case REMINDER_VIBRATION_DOUBLE:
      vibes_double_pulse();
      break;
    case REMINDER_VIBRATION_LONG:
      vibes_long_pulse();
      break;
  }
}

static int32_t write_setting(int key, int32_t value) {
  const int result = persist_write_int(key, value);
  return result == sizeof(value) ? 0 : result < 0 ? result : E_INTERNAL;
}

int32_t worker_active_interval(void) {
  return app_worker_is_running() && worker_monitoring_enabled() &&
         !persist_read_bool(WORKER_MONITORING_PAUSED_KEY)
      ? worker_read_settings().interval_minutes * ACTIVITY_MINUTE_SECONDS : 0;
}

int32_t worker_monitoring_paused(void) {
  return worker_monitoring_enabled() && persist_read_bool(WORKER_MONITORING_PAUSED_KEY);
}

int32_t worker_is_running(void) {
  return app_worker_is_running();
}

int32_t worker_settings_value(int32_t field) {
  const WorkerSettings settings = worker_read_settings();
  switch (field) {
    case 0: return settings.interval_minutes;
    case 1: return settings.window_minutes;
    case 2: return settings.step_threshold;
    case 3: return settings.vibration;
    default: return E_INVALID_ARGUMENT;
  }
}

int32_t worker_save_settings(int32_t interval, int32_t window, int32_t steps, int32_t vibration) {
  const WorkerSettings previous = worker_read_settings();
  WorkerSettings settings = {
    .version = WORKER_SETTINGS_VERSION,
    .detection_generation = previous.detection_generation,
    .interval_minutes = interval,
    .window_minutes = window,
    .step_threshold = steps,
    .vibration = vibration,
  };
  const WorkerSettingsValidation validation = worker_validate_settings(&settings);
  if (validation != SETTINGS_VALID) {
    return validation;
  }

  const bool detection_changed = worker_detection_changed(&previous, &settings);
  settings.detection_generation += detection_changed;
  const int written = persist_write_data(WORKER_SETTINGS_KEY, &settings, sizeof(settings));
  if (written != sizeof(settings)) {
    return written < 0 ? written : E_INTERNAL;
  }
  if (detection_changed && worker_active_interval()) {
    AppWorkerMessage message = {0};
    app_worker_send_message(WORKER_SETTINGS_CHANGED, &message);
  }
  return 0;
}

int32_t worker_start(void) {
  const bool running = app_worker_is_running();
  if (worker_active_interval()) {
    return APP_WORKER_RESULT_SUCCESS;
  }

  int32_t result = write_setting(WORKER_SESSION_KEY, (uint16_t)(worker_current_session() + 1));
  if (result) {
    return result;
  }
  result = write_setting(WORKER_MONITORING_KEY, WORKER_MONITORING_ENABLED);
  if (result) {
    return result;
  }
  if (persist_read_bool(WORKER_MONITORING_PAUSED_KEY)) {
    result = persist_delete(WORKER_MONITORING_PAUSED_KEY);
    if (result < 0) {
      return result;
    }
  }

  // An earlier comparison run could otherwise interrupt the foreground app.
  wakeup_cancel_all();
  AppWorkerResult launch_result = running
      ? APP_WORKER_RESULT_ALREADY_RUNNING
      : app_worker_launch();
  if (launch_result == APP_WORKER_RESULT_ALREADY_RUNNING) {
    AppWorkerMessage message = {0};
    app_worker_send_message(WORKER_START_REQUESTED, &message);
    return APP_WORKER_RESULT_SUCCESS;
  }
  return launch_result;
}

int32_t worker_acknowledge_activity(void) {
  if (!worker_active_interval()) {
    return APP_WORKER_RESULT_NOT_RUNNING;
  }
  s_acknowledged_at = time(NULL);
  s_acknowledgment_session = worker_current_session();
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
  return s_acknowledgment_received && worker_current_session() == s_acknowledgment_session;
}

int32_t worker_stop(void) {
  // Invalidate queued reminders before asking the OS to stop the worker.
  const int32_t result = write_setting(WORKER_MONITORING_KEY, 0);
  if (result) {
    return result;
  }
  const AppWorkerResult stopped = app_worker_is_running()
      ? app_worker_kill() : APP_WORKER_RESULT_NOT_RUNNING;
  return stopped == APP_WORKER_RESULT_NOT_RUNNING ? APP_WORKER_RESULT_SUCCESS : stopped;
}

static bool read_pending_reminder(WorkerReminder *reminder) {
  if (!worker_active_interval() ||
      persist_read_data(WORKER_REMINDER_KEY, reminder, sizeof(*reminder)) != sizeof(*reminder)) {
    return false;
  }
  const uint32_t now = time(NULL);
  return reminder->session == worker_current_session() &&
         reminder->detection_generation == worker_read_settings().detection_generation &&
         reminder->issued_at <= now &&
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
      !worker_active_interval() || reminder.session != worker_current_session()) {
    return UINT32_MAX;
  }
  const WorkerSettings settings = worker_read_settings();
  if (reminder.detection_generation != settings.detection_generation) {
    return UINT32_MAX;
  }
  vibrate_reminder(settings.vibration);
  APP_LOG(APP_LOG_LEVEL_INFO, "Reminder displayed issued_at=%lu last_activity=%lu vibration=%ld",
          (unsigned long)reminder.issued_at, (unsigned long)reminder.last_activity_at,
          (long)settings.vibration);
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
