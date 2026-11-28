#include <pebble_worker.h>
#include <inttypes.h>

#include "../../src/c/worker_storage.h"

enum { HEALTH_BATCH_MINUTES = 16 };

static AppTimer *s_timer;
static bool s_waiting_to_retry;
static InactivitySettings s_settings;
static InactivitySession s_session;
static uint16_t s_session_id;
static uint32_t s_detection_generation;
static time_t s_due_at;

static void schedule_check(time_t due_at, bool allow_retry);
static void cancel_check(void);
static bool reload_configuration(time_t now);

static bool session_is_current(void) {
  return worker_monitoring_enabled() && !persist_read_bool(WORKER_MONITORING_PAUSED_KEY) &&
         s_session_id == worker_current_session();
}

static bool detection_is_current(void) {
  return s_detection_generation == worker_read_settings().detection_generation;
}

static void read_history(InactivityHistory *history) {
  HealthMinuteData records[HEALTH_BATCH_MINUTES];
  while (history->next_minute < history->end) {
    const time_t requested_start = history->next_minute;
    time_t requested_end = requested_start + HEALTH_BATCH_MINUTES * ACTIVITY_MINUTE_SECONDS;
    if (requested_end > history->end) {
      requested_end = history->end;
    }
    time_t start = requested_start;
    time_t end = requested_end;
    const uint32_t capacity = (requested_end - requested_start) / ACTIVITY_MINUTE_SECONDS;
    const uint32_t count = health_service_get_minute_history(records, capacity, &start, &end);
    if (!count || count > capacity || start < requested_start || end > requested_end ||
        start % ACTIVITY_MINUTE_SECONDS || end != start + (time_t)count * ACTIVITY_MINUTE_SECONDS) {
      while (history->next_minute < requested_end) {
        inactivity_history_add(history, &s_settings, ACTIVITY_UNKNOWN_STEPS);
      }
      continue;
    }

    while (history->next_minute < start) {
      inactivity_history_add(history, &s_settings, ACTIVITY_UNKNOWN_STEPS);
    }
    for (uint32_t i = 0; i < count; i++) {
      inactivity_history_add(history, &s_settings,
                             records[i].is_invalid ? ACTIVITY_UNKNOWN_STEPS : records[i].steps);
    }
    // A short read can stop at a gap. Query the remaining range on the next iteration.
  }
}

static void present_reminder(time_t issued_at, time_t last_activity_at) {
  if (!session_is_current() || !detection_is_current()) {
    return;
  }
  const WorkerReminder reminder = {
    .issued_at = issued_at,
    .detection_generation = s_detection_generation,
    .last_activity_at = last_activity_at,
    .session = s_session_id,
  };
  const int result = persist_write_data(WORKER_REMINDER_KEY, &reminder, sizeof(reminder));
  if (result != sizeof(reminder)) {
    APP_LOG(APP_LOG_LEVEL_ERROR, "Could not save reminder (%d)", result);
    return;
  }
  if (session_is_current() && detection_is_current()) {
    APP_LOG(APP_LOG_LEVEL_INFO, "Reminder requested at=%lu last_activity=%lu",
            (unsigned long)issued_at, (unsigned long)last_activity_at);
    AppWorkerMessage message = {0};
    app_worker_send_message(WORKER_REMINDER_REQUESTED, &message);
    worker_launch_app();
  }
}

static void evaluate_monitoring(time_t now) {
  InactivityHistory history = inactivity_history_begin(&s_settings, &s_session, now);
  read_history(&history);
  if (!session_is_current()) {
    return;
  }
  if (!detection_is_current()) {
    schedule_check(now, true);
    return;
  }
  const InactivityDecision decision = inactivity_evaluate(&s_settings, &s_session, &history, now);
  s_session.detected_at = history.detected_at;
  s_session.history_end = history.end;
  s_session.history_retry = decision.history_retry;
  if (decision.stop_monitoring) {
    APP_LOG(APP_LOG_LEVEL_INFO, "Monitoring stopped: Health history unavailable for one hour");
    cancel_check();
    s_session = (InactivitySession){0};
    const status_t result = persist_write_bool(WORKER_MONITORING_PAUSED_KEY, true);
    if (result < 0) {
      APP_LOG(APP_LOG_LEVEL_ERROR, "Could not save paused monitoring state (%d)", (int)result);
    }
    return;
  }

  APP_LOG(APP_LOG_LEVEL_INFO, "Inactivity check last_activity=%lu next=%lu status=%s",
          (unsigned long)decision.last_activity_at, (unsigned long)decision.next_check_at,
          decision.history_retry.interval_seconds ? "history_pending" :
          decision.request_reminder ? "reminder" : "waiting");

  time_t next_check_at = decision.next_check_at;
  if (decision.request_reminder) {
    s_session.reminded_at = time(NULL);
    next_check_at += s_session.reminded_at - now;
    schedule_check(next_check_at, true);
    present_reminder(s_session.reminded_at, decision.last_activity_at);
    return;
  }
  schedule_check(next_check_at, true);
}

static void check_history(void *context) {
  s_timer = NULL;
  if (!session_is_current()) {
    return;
  }
  const time_t now = time(NULL);
  APP_LOG(APP_LOG_LEVEL_INFO, "Worker check due=%lu actual=%lu delay=%ld s",
          (unsigned long)s_due_at, (unsigned long)now, (long)now - (long)s_due_at);
  if (!reload_configuration(now)) {
    evaluate_monitoring(now);
  }
}

static void retry_timer(struct tm *tick_time, TimeUnits units_changed) {
  tick_timer_service_unsubscribe();
  s_waiting_to_retry = false;
  if (!reload_configuration(time(NULL))) {
    schedule_check(s_due_at, false);
  }
}

static void schedule_check(time_t due_at, bool allow_retry) {
  if (!session_is_current()) {
    return;
  }
  s_due_at = due_at;
  const time_t now = time(NULL);
  const uint32_t delay_ms = due_at > now ? (uint32_t)(due_at - now) * 1000 : 1;
  s_timer = app_timer_register(delay_ms, check_history, NULL);
  if (s_timer) {
    APP_LOG(APP_LOG_LEVEL_INFO, "Worker scheduled interval=%" PRId32 " s due=%lu",
            s_settings.interval_seconds, (unsigned long)s_due_at);
  } else {
    APP_LOG(APP_LOG_LEVEL_ERROR, "Worker timer registration failed");
    if (allow_retry) {
      s_waiting_to_retry = true;
      tick_timer_service_subscribe(MINUTE_UNIT, retry_timer);
    }
  }
}

static void cancel_check(void) {
  if (s_timer) {
    app_timer_cancel(s_timer);
    s_timer = NULL;
  }
  if (s_waiting_to_retry) {
    tick_timer_service_unsubscribe();
    s_waiting_to_retry = false;
  }
}

// Returns true when it handled the check by stopping, starting, or rescheduling.
static bool reload_configuration(time_t now) {
  if (!worker_monitoring_enabled() || persist_read_bool(WORKER_MONITORING_PAUSED_KEY)) {
    cancel_check();
    s_session = (InactivitySession){0};
    return true;
  }

  const uint16_t session_id = worker_current_session();
  const WorkerSettings saved = worker_read_settings();
  const InactivitySettings next = {
    .interval_seconds = saved.interval_minutes * ACTIVITY_MINUTE_SECONDS,
    .window_minutes = saved.window_minutes,
    .step_threshold = saved.step_threshold,
  };
  const bool session_changed = !s_session.started_at || s_session_id != session_id;
  const bool clock_reset = !session_changed && inactivity_clock_needs_reset(&s_session, s_due_at, now);
  const bool new_session = session_changed || clock_reset;
  const bool rule_changed = s_settings.window_minutes != next.window_minutes ||
                            s_settings.step_threshold != next.step_threshold;
  if (!new_session && !rule_changed && s_settings.interval_seconds == next.interval_seconds &&
      s_detection_generation == saved.detection_generation) {
    return false;
  }

  cancel_check();
  s_session_id = session_id;
  s_detection_generation = saved.detection_generation;
  s_settings = next;
  if (new_session) {
    if (clock_reset) {
      APP_LOG(APP_LOG_LEVEL_INFO, "Watch clock changed: restarting monitoring at=%lu", (unsigned long)now);
    }
    s_session = (InactivitySession){.started_at = now};
    schedule_check(now + s_settings.interval_seconds, true);
  } else {
    if (rule_changed) {
      inactivity_forget_detection(&s_session, now);
    }
    s_session.history_retry = (HistoryRetry){0};
    evaluate_monitoring(now);
  }
  return true;
}

static void on_worker_message(uint16_t type, AppWorkerMessage *message) {
  if (type == WORKER_START_REQUESTED || type == WORKER_SETTINGS_CHANGED) {
    reload_configuration(time(NULL));
  } else if (type == WORKER_ACTIVITY_ACKNOWLEDGED && s_session.started_at &&
             message->data2 == s_session_id && session_is_current()) {
    const time_t acknowledged_at = ((uint32_t)message->data1 << 16) | message->data0;
    const time_t now = time(NULL);
    if (acknowledged_at < s_session.started_at || acknowledged_at > now ||
        acknowledged_at < s_session.acknowledged_at || acknowledged_at < s_session.reminded_at) {
      return;
    }
    if (acknowledged_at > s_session.acknowledged_at) {
      s_session.acknowledged_at = acknowledged_at;
      s_session.history_retry = (HistoryRetry){0};
      cancel_check();
      schedule_check(acknowledged_at + s_settings.interval_seconds, true);
      APP_LOG(APP_LOG_LEVEL_INFO, "Activity acknowledged at=%lu", (unsigned long)acknowledged_at);
    }
    app_worker_send_message(WORKER_ACTIVITY_ACKNOWLEDGED_RESULT, message);
  }
}

int main(void) {
  if (!app_worker_message_subscribe(on_worker_message)) {
    APP_LOG(APP_LOG_LEVEL_ERROR, "Worker could not subscribe to settings changes");
    return 0;
  }
  const WatchInfoVersion firmware = watch_info_get_firmware_version();
  APP_LOG(APP_LOG_LEVEL_INFO, "Activity Reminder worker started firmware=%u.%u.%u",
          (unsigned int)firmware.major, (unsigned int)firmware.minor, (unsigned int)firmware.patch);
  reload_configuration(time(NULL));
  worker_event_loop();
  cancel_check();
  app_worker_message_unsubscribe();
  APP_LOG(APP_LOG_LEVEL_INFO, "Activity Reminder worker stopped");
  return 0;
}
