#include <pebble_worker.h>
#include <inttypes.h>

#include "../../src/c/worker_settings.h"
#include "inactivity.h"

enum { HEALTH_BATCH_MINUTES = 16 };

static AppTimer *s_timer;
static bool s_waiting_to_retry;
static InactivitySettings s_settings = {
  .window_minutes = ACTIVITY_DEFAULT_WINDOW,
  .step_threshold = ACTIVITY_DEFAULT_STEPS,
};
static InactivitySession s_session;
static uint16_t s_session_id;
static time_t s_due_at;

static void schedule_check(time_t due_at, bool allow_retry);
static void cancel_check(void);

static bool session_is_current(void) {
  return s_settings.interval_seconds > 0 &&
         s_settings.interval_seconds == persist_read_int(WORKER_INTERVAL_KEY) &&
         s_session_id == (uint16_t)persist_read_int(WORKER_SESSION_KEY);
}

static int read_diagnostic_minute(time_t minute, time_t observed) {
  time_t start = minute;
  time_t end = minute + 60;
  HealthMinuteData record;
  const uint32_t count = health_service_get_minute_history(&record, 1, &start, &end);
  if (count == 0 || start != minute || end != minute + 60) {
    APP_LOG(APP_LOG_LEVEL_INFO, "Health minute=%lu observed=%lu status=missing",
            (unsigned long)minute, (unsigned long)observed);
  } else if (record.is_invalid) {
    APP_LOG(APP_LOG_LEVEL_INFO, "Health minute=%lu observed=%lu status=invalid",
            (unsigned long)minute, (unsigned long)observed);
  } else {
    APP_LOG(APP_LOG_LEVEL_INFO, "Health minute=%lu observed=%lu status=valid steps=%u vmc=%u",
            (unsigned long)minute, (unsigned long)observed,
            (unsigned int)record.steps, (unsigned int)record.vmc);
    return record.steps;
  }
  return ACTIVITY_UNKNOWN_STEPS;
}

static void log_activity_window(time_t end, int previous_steps, int current_steps) {
  if (previous_steps == ACTIVITY_UNKNOWN_STEPS || current_steps == ACTIVITY_UNKNOWN_STEPS) {
    APP_LOG(APP_LOG_LEVEL_INFO, "Activity window end=%lu threshold=%d status=unknown",
            (unsigned long)end, ACTIVITY_DEFAULT_STEPS);
    return;
  }

  const int steps = previous_steps + current_steps;
  APP_LOG(APP_LOG_LEVEL_INFO, "Activity window end=%lu steps=%d threshold=%d status=%s",
          (unsigned long)end, steps, ACTIVITY_DEFAULT_STEPS,
          steps >= ACTIVITY_DEFAULT_STEPS ? "qualifying" : "below_threshold");
}

static void run_diagnostics(time_t now) {
  const time_t minute_boundary = now - now % 60;
  const int history_minutes = 4;
  int previous_steps = ACTIVITY_UNKNOWN_STEPS;
  for (int age_minutes = history_minutes; age_minutes > 0; age_minutes--) {
    const time_t minute = minute_boundary - age_minutes * 60;
    const int steps = read_diagnostic_minute(minute, now);
    if (age_minutes < history_minutes) {
      log_activity_window(minute + 60, previous_steps, steps);
    }
    previous_steps = steps;
  }
  schedule_check(now + WORKER_DIAGNOSTIC_INTERVAL_SECONDS, true);
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
  if (!session_is_current()) {
    return;
  }
  const WorkerReminder reminder = {
    .issued_at = issued_at,
    .last_activity_at = last_activity_at,
    .session = s_session_id,
  };
  const int result = persist_write_data(WORKER_REMINDER_KEY, &reminder, sizeof(reminder));
  if (result != sizeof(reminder)) {
    APP_LOG(APP_LOG_LEVEL_ERROR, "Could not save reminder (%d)", result);
    return;
  }
  if (session_is_current()) {
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
  if (s_settings.interval_seconds == WORKER_DIAGNOSTIC_INTERVAL_SECONDS) {
    run_diagnostics(now);
  } else {
    if (now < s_session.history_end || now < s_session.acknowledged_at || now < s_session.started_at ||
        now < s_session.reminded_at) {
      s_session = (InactivitySession){.started_at = now};
    }
    evaluate_monitoring(now);
  }
}

static void retry_timer(struct tm *tick_time, TimeUnits units_changed) {
  tick_timer_service_unsubscribe();
  s_waiting_to_retry = false;
  schedule_check(s_due_at, false);
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

static void load_interval(void) {
  cancel_check();
  if (persist_read_bool(WORKER_MONITORING_PAUSED_KEY)) {
    const status_t result = persist_delete(WORKER_MONITORING_PAUSED_KEY);
    if (result < 0) {
      APP_LOG(APP_LOG_LEVEL_ERROR, "Could not clear paused monitoring state (%d)", (int)result);
      return;
    }
  }
  const time_t now = time(NULL);
  const uint16_t session_id = persist_read_int(WORKER_SESSION_KEY);
  const bool was_monitoring = s_settings.interval_seconds >= WORKER_MIN_INTERVAL_SECONDS &&
                              s_session_id == session_id;
  s_session_id = session_id;
  s_settings.interval_seconds = persist_read_int(WORKER_INTERVAL_KEY);
  if (s_settings.interval_seconds == WORKER_DIAGNOSTIC_INTERVAL_SECONDS) {
    s_session = (InactivitySession){0};
    schedule_check(now + WORKER_DIAGNOSTIC_INTERVAL_SECONDS, true);
  } else if (s_settings.interval_seconds < WORKER_MIN_INTERVAL_SECONDS ||
             s_settings.interval_seconds > WORKER_MAX_INTERVAL_SECONDS ||
             !inactivity_settings_valid(&s_settings)) {
    s_session = (InactivitySession){0};
    APP_LOG(APP_LOG_LEVEL_INFO, "Worker is waiting for monitoring to start");
  } else if (!was_monitoring || !s_session.started_at || now < s_session.history_end) {
    s_session = (InactivitySession){.started_at = now};
    schedule_check(now + s_settings.interval_seconds, true);
  } else {
    evaluate_monitoring(now);
  }
}

static void on_worker_message(uint16_t type, AppWorkerMessage *message) {
  if (type == WORKER_INTERVAL_CHANGED) {
    load_interval();
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
  if (!persist_read_bool(WORKER_MONITORING_PAUSED_KEY)) {
    load_interval();
  }
  worker_event_loop();
  cancel_check();
  app_worker_message_unsubscribe();
  APP_LOG(APP_LOG_LEVEL_INFO, "Activity Reminder worker stopped");
  return 0;
}
