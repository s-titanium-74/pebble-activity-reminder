#include <assert.h>
#include <stddef.h>
#include <stdio.h>

#include "../src/c/worker_settings.h"

#define AT(hour, minute) ((time_t)((hour) * 3600 + (minute) * 60))

typedef struct {
  time_t start;
  int steps;
} MinuteSample;

static const InactivitySettings settings = {
  .interval_seconds = 30 * 60,
  .window_minutes = ACTIVITY_DEFAULT_WINDOW,
  .step_threshold = ACTIVITY_DEFAULT_STEPS,
};

static InactivityDecision check(const InactivitySettings *config, InactivitySession *session, time_t now,
                                int default_steps, const MinuteSample *samples, size_t count) {
  InactivityHistory history = inactivity_history_begin(config, session, now);
  while (history.next_minute < history.end) {
    int steps = default_steps;
    for (size_t i = 0; i < count; i++) {
      if (samples[i].start == history.next_minute) {
        steps = samples[i].steps;
      }
    }
    inactivity_history_add(&history, config, steps);
  }
  const InactivityDecision decision = inactivity_evaluate(config, session, &history, now);
  session->history_end = history.end;
  session->detected_at = history.detected_at;
  session->history_retry = decision.history_retry;
  if (decision.request_reminder) {
    session->reminded_at = now;
  }
  return decision;
}

static void expect(InactivityDecision decision, time_t activity, time_t next, bool reminder, bool pending) {
  assert(decision.last_activity_at == activity);
  assert(decision.next_check_at == next);
  assert(decision.request_reminder == reminder);
  assert((decision.history_retry.interval_seconds > 0) == pending);
  assert(!decision.stop_monitoring);
}

static void test_activity_between_checks(void) {
  InactivitySession session = {.started_at = AT(11, 30)};
  const MinuteSample original_activity[] = {{AT(11, 38), 10}, {AT(11, 39), 10}};
  expect(check(&settings, &session, AT(12, 0), 0, original_activity, 2),
         AT(11, 40), AT(12, 10), false, false);

  const MinuteSample new_activity[] = {{AT(12, 3), 10}, {AT(12, 4), 10}};
  expect(check(&settings, &session, AT(12, 10), 0, new_activity, 2),
         AT(12, 5), AT(12, 35), false, false);
  expect(check(&settings, &session, AT(12, 35) - 1, 0, new_activity, 2),
         AT(12, 5), AT(12, 35), false, false);
  expect(check(&settings, &session, AT(12, 35), 0, new_activity, 2),
         AT(12, 5), AT(13, 5), true, false);
  // Dismissing a reminder leaves the session in the same state.
  expect(check(&settings, &session, AT(13, 5), 0, NULL, 0),
         AT(12, 5), AT(13, 35), true, false);
}

static void test_start_and_no_activity(void) {
  InactivitySession session = {.started_at = AT(11, 59) + 31};
  const MinuteSample starting_minute[] = {{AT(11, 59), 100}};
  expect(check(&settings, &session, AT(12, 29) + 30, 0, starting_minute, 1),
         0, AT(12, 29) + 31, false, false);
  expect(check(&settings, &session, AT(12, 29) + 31, 0, starting_minute, 1),
         0, AT(12, 59) + 31, true, false);
}

static void test_manual_acknowledgment(void) {
  InactivitySession session = {.started_at = AT(12, 0), .acknowledged_at = AT(12, 28) + 15};
  const MinuteSample older_activity[] = {{AT(12, 24), 20}};
  expect(check(&settings, &session, AT(12, 30), 0, older_activity, 1),
         AT(12, 28) + 15, AT(12, 58) + 15, false, false);
  const MinuteSample newer_activity[] = {{AT(12, 31), 10}, {AT(12, 32), 10}};
  expect(check(&settings, &session, AT(12, 58) + 15, 0, newer_activity, 2),
         AT(12, 33), AT(13, 3), false, false);
}

static void test_delayed_and_unavailable_history(void) {
  InactivitySession session = {.started_at = AT(12, 0)};
  const MinuteSample delayed[] = {{AT(12, 28), 20}, {AT(12, 29), ACTIVITY_UNKNOWN_STEPS},
                                  {AT(12, 27), ACTIVITY_UNKNOWN_STEPS}};
  expect(check(&settings, &session, AT(12, 30), 0, delayed, 3),
         0, AT(12, 31), false, true);
  const MinuteSample arrived[] = {{AT(12, 28), 20}, {AT(12, 29), 0}};
  expect(check(&settings, &session, AT(12, 31), 0, arrived, 2),
         AT(12, 30), AT(13, 0), false, false);

  expect(check(&settings, &session, AT(13, 0), ACTIVITY_UNKNOWN_STEPS, NULL, 0),
         AT(12, 30), AT(13, 1), false, true);
  expect(check(&settings, &session, AT(13, 1), ACTIVITY_UNKNOWN_STEPS, NULL, 0),
         AT(12, 30), AT(13, 3), false, true);
  const MinuteSample gap[] = {{AT(13, 0), ACTIVITY_UNKNOWN_STEPS}};
  expect(check(&settings, &session, AT(13, 31), 0, gap, 1),
         AT(12, 30), AT(13, 35), false, true);
  expect(check(&settings, &session, AT(13, 35), 0, gap, 1),
         AT(12, 30), AT(14, 5), true, false);
}

static void test_history_backoff_and_shutdown(void) {
  InactivitySession session = {.started_at = AT(11, 30)};
  const int retry_minutes[] = {0, 1, 3, 7, 15, 30, 45, 60};
  for (size_t i = 0; i + 1 < sizeof(retry_minutes) / sizeof(retry_minutes[0]); i++) {
    expect(check(&settings, &session, AT(12, retry_minutes[i]), ACTIVITY_UNKNOWN_STEPS, NULL, 0),
           0, AT(12, retry_minutes[i + 1]), false, true);
  }

  InactivitySession recovered = session;
  expect(check(&settings, &recovered, AT(13, 0), 0, NULL, 0), 0, AT(13, 30), true, false);
  expect(check(&settings, &recovered, AT(13, 30), ACTIVITY_UNKNOWN_STEPS, NULL, 0),
         0, AT(13, 31), false, true);
  assert(recovered.history_retry.started_at == AT(13, 30));

  // A late retry uses the remaining time to the cutoff instead of extending it.
  expect(check(&settings, &session, AT(12, 49), ACTIVITY_UNKNOWN_STEPS, NULL, 0),
         0, AT(13, 0), false, true);
  const InactivityDecision stopped = check(&settings, &session, AT(13, 0), ACTIVITY_UNKNOWN_STEPS, NULL, 0);
  assert(stopped.stop_monitoring);
  assert(!stopped.request_reminder);
  assert(stopped.next_check_at == 0);

  InactivitySession restarted = {.started_at = AT(13, 5)};
  expect(check(&settings, &restarted, AT(13, 35), ACTIVITY_UNKNOWN_STEPS, NULL, 0),
         0, AT(13, 36), false, true);
}

static void test_late_callback_and_configurable_rule(void) {
  const InactivitySettings custom = {.interval_seconds = 15 * 60, .window_minutes = 3, .step_threshold = 9};
  assert(inactivity_settings_valid(&custom));
  InactivitySession session = {.started_at = AT(12, 0), .history_end = AT(12, 15)};
  const MinuteSample samples[] = {{AT(12, 14), 3}, {AT(12, 15), 3}, {AT(12, 16), 3}};
  expect(check(&custom, &session, AT(12, 45), 0, samples, 3),
         AT(12, 17), AT(13, 0), true, false);

  InactivitySession first_check = {.started_at = AT(12, 0) + 30};
  expect(check(&custom, &first_check, AT(12, 45), 0, samples, 3),
         AT(12, 17), AT(13, 0), true, false);
}

static void test_clock_sync_after_reboot(void) {
  const time_t before_sync = 1262304003;
  const time_t after_sync = 1795606229;
  const int interval = 2 * ACTIVITY_MINUTE_SECONDS;
  const InactivitySession session = {.started_at = before_sync};
  const time_t due = before_sync + interval;

  assert(!inactivity_clock_needs_reset(&session, due, due));
  assert(!inactivity_clock_needs_reset(&session, due, due + 3600));
  assert(inactivity_clock_needs_reset(&session, due, due + 3601));
  assert(inactivity_clock_needs_reset(&session, due, after_sync));

  const InactivitySession restarted = {.started_at = after_sync};
  assert(!inactivity_clock_needs_reset(&restarted, after_sync + interval, after_sync));
  assert(inactivity_clock_needs_reset(&restarted, after_sync + interval, before_sync));
}

static void test_settings_validation(void) {
  const WorkerSettings defaults = {
    .interval_minutes = 30, .window_minutes = 2, .step_threshold = 20,
    .vibration = REMINDER_VIBRATION_SHORT,
  };
  WorkerSettings saved = defaults;
  assert(worker_validate_settings(&saved) == SETTINGS_VALID);
  saved.vibration = REMINDER_VIBRATION_LONG;
  assert(worker_validate_settings(&saved) == SETTINGS_VALID);
  assert(!worker_detection_changed(&defaults, &saved));
  saved.vibration = REMINDER_VIBRATION_LONG + 1;
  assert(worker_validate_settings(&saved) == SETTINGS_INVALID_VIBRATION);

  saved = (WorkerSettings){.interval_minutes = 2, .window_minutes = 1, .step_threshold = 1};
  assert(worker_validate_settings(&saved) == SETTINGS_VALID);
  assert(worker_detection_changed(&defaults, &saved));
  saved.interval_minutes = 1;
  assert(worker_validate_settings(&saved) == SETTINGS_INVALID_INTERVAL);
  saved.interval_minutes = 61;
  assert(worker_validate_settings(&saved) == SETTINGS_INVALID_INTERVAL);
  saved.interval_minutes = 2;
  saved.window_minutes = 3;
  assert(worker_validate_settings(&saved) == SETTINGS_INVALID_WINDOW);
  saved.window_minutes = 0;
  assert(worker_validate_settings(&saved) == SETTINGS_INVALID_WINDOW);
  saved.window_minutes = 1;
  saved.step_threshold = 0;
  assert(worker_validate_settings(&saved) == SETTINGS_INVALID_STEPS);

  saved = (WorkerSettings){.interval_minutes = 60, .window_minutes = 60, .step_threshold = 15300};
  assert(worker_validate_settings(&saved) == SETTINGS_VALID);
  saved.step_threshold++;
  assert(worker_validate_settings(&saved) == SETTINGS_INVALID_STEPS);
}

static void test_settings_change_during_session(void) {
  const InactivitySettings shorter = {.interval_seconds = 5 * 60, .window_minutes = 2, .step_threshold = 40};
  InactivitySession session = {
    .started_at = AT(11, 0),
    .history_end = AT(11, 58),
    .detected_at = AT(11, 58),
    .acknowledged_at = AT(11, 50),
    .reminded_at = AT(11, 55),
  };
  const time_t now = AT(12, 0);
  const MinuteSample old_rule_only[] = {{AT(11, 57), 20}};
  expect(check(&settings, &session, now, 0, old_rule_only, 1),
         AT(11, 59), AT(12, 29), false, false);
  inactivity_forget_detection(&session, now);
  const InactivityHistory history = inactivity_history_begin(&shorter, &session, now);
  assert(history.next_minute == AT(11, 53));
  assert(session.started_at == AT(11, 0));
  assert(session.reminded_at == AT(11, 55));

  InactivitySession missing_history = session;
  expect(check(&shorter, &missing_history, now, ACTIVITY_UNKNOWN_STEPS, NULL, 0),
         AT(11, 50), AT(12, 1), false, true);
  expect(check(&shorter, &session, now, 0, old_rule_only, 1),
         AT(11, 50), AT(12, 5), true, false);
  expect(check(&shorter, &session, AT(12, 4), 0, NULL, 0),
         AT(11, 50), AT(12, 5), false, false);
}

int main(void) {
  test_activity_between_checks();
  test_start_and_no_activity();
  test_manual_acknowledgment();
  test_delayed_and_unavailable_history();
  test_history_backoff_and_shutdown();
  test_late_callback_and_configurable_rule();
  test_clock_sync_after_reboot();
  test_settings_validation();
  test_settings_change_during_session();
  puts("Inactivity tests passed");
  return 0;
}
