#include <pebble.h>
#include <inttypes.h>

static const uint32_t trial_key = 1;

enum { FIRST_WAKEUP = 1, FINAL_WAKEUP = 2 };

typedef struct {
  WakeupId id;
  int32_t cookie;
  uint32_t due_at;
  uint32_t started_at;
  uint32_t interval_seconds;
} WakeupTrial;

static int32_t save_trial(const WakeupTrial *trial) {
  int written = persist_write_data(trial_key, trial, sizeof(*trial));
  if (written == (int)sizeof(*trial)) {
    return 0;
  }
  return written < 0 ? written : E_INTERNAL;
}

int32_t save_initial_wakeup(int32_t id, uint32_t due_at, uint32_t started_at,
                            uint32_t interval_seconds) {
  WakeupTrial trial = { id, FIRST_WAKEUP, due_at, started_at, interval_seconds };
  return save_trial(&trial);
}

void handle_spike_wakeup(void) {
  const time_t launched_at = time(NULL);
  WakeupId id;
  int32_t cookie;
  if (!wakeup_get_launch_event(&id, &cookie)) {
    APP_LOG(APP_LOG_LEVEL_ERROR, "Wakeup Spike missing launch event");
    return;
  }

  WakeupTrial trial;
  int read = persist_read_data(trial_key, &trial, sizeof(trial));
  if (read != (int)sizeof(trial) || trial.id != id || trial.cookie != cookie) {
    APP_LOG(APP_LOG_LEVEL_ERROR, "Unexpected wakeup id=%" PRId32 " cookie=%" PRId32 " read=%d",
            id, cookie, read);
    return;
  }

  APP_LOG(APP_LOG_LEVEL_INFO, "Read trial marker=%" PRIu32 " id=%" PRId32 " cookie=%" PRId32,
          trial.started_at, id, cookie);
  APP_LOG(APP_LOG_LEVEL_INFO, "Wakeup due=%" PRIu32 " actual=%lu delay=%ld s",
          trial.due_at, (unsigned long)launched_at, (long)launched_at - (long)trial.due_at);

  status_t deleted = persist_delete(trial_key);
  if (deleted < 0) {
    APP_LOG(APP_LOG_LEVEL_ERROR, "Wakeup Spike persist_delete failed: %d", (int)deleted);
    return;
  }
  if (cookie == FINAL_WAKEUP) {
    APP_LOG(APP_LOG_LEVEL_INFO, "Trial complete, exiting without a Window");
    return;
  }

  trial.cookie = FINAL_WAKEUP;
  trial.due_at = time(NULL) + trial.interval_seconds;
  trial.id = wakeup_schedule(trial.due_at, trial.cookie, false);
  if (trial.id < 0) {
    APP_LOG(APP_LOG_LEVEL_ERROR, "Wakeup Spike wakeup_schedule failed: %" PRId32, trial.id);
    return;
  }
  int32_t result = save_trial(&trial);
  if (result != 0) {
    wakeup_cancel(trial.id);
    APP_LOG(APP_LOG_LEVEL_ERROR, "Wakeup Spike persist_write_data failed: %" PRId32, result);
    return;
  }
  APP_LOG(APP_LOG_LEVEL_INFO, "Reserved id=%" PRId32 " cookie=%" PRId32 " due=%" PRIu32
          ", exiting without a Window", trial.id, trial.cookie, trial.due_at);
}
