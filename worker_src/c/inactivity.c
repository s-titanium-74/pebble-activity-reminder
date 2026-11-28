#include "inactivity.h"

enum {
  HISTORY_RETRY_MAX_SECONDS = 15 * ACTIVITY_MINUTE_SECONDS,
  HISTORY_TIMEOUT_SECONDS = 60 * ACTIVITY_MINUTE_SECONDS,
  CLOCK_RESET_THRESHOLD_SECONDS = 60 * ACTIVITY_MINUTE_SECONDS,
};

static time_t later(time_t a, time_t b) {
  return a > b ? a : b;
}

bool inactivity_settings_valid(const InactivitySettings *settings) {
  return settings->window_minutes >= 1 && settings->window_minutes <= ACTIVITY_MAX_WINDOW &&
         settings->step_threshold >= 1 && settings->step_threshold <= settings->window_minutes * UINT8_MAX &&
         settings->interval_seconds >= settings->window_minutes * ACTIVITY_MINUTE_SECONDS;
}

bool inactivity_clock_needs_reset(const InactivitySession *session, time_t next_check_at, time_t now) {
  if (now < session->started_at || now < session->history_end ||
      now < session->acknowledged_at || now < session->reminded_at) {
    return true;
  }
  // Allow ordinary late callbacks and timer retries to catch up on history.
  return next_check_at && (int64_t)now - (int64_t)next_check_at > CLOCK_RESET_THRESHOLD_SECONDS;
}

void inactivity_forget_detection(InactivitySession *session, time_t now) {
  session->detected_at = 0;
  // Use the new interval and window to bound the read after a rule change.
  session->history_end = now - now % ACTIVITY_MINUTE_SECONDS;
}

InactivityHistory inactivity_history_begin(const InactivitySettings *settings,
                                           const InactivitySession *session, time_t now) {
  const time_t minute = ACTIVITY_MINUTE_SECONDS;
  const time_t end = now - now % minute;
  const time_t recent_after = now > settings->interval_seconds ? now - settings->interval_seconds : 0;
  time_t boundary = recent_after - recent_after % minute;
  // A window ending at the cutoff can still supply the last activity timestamp.
  if (boundary && boundary == recent_after) {
    boundary -= minute;
  }
  const time_t previous_end = session->history_end ? session->history_end : session->started_at;
  if (previous_end < boundary) {
    boundary = previous_end;
  }
  const time_t overlap = (settings->window_minutes - 1) * minute;
  const time_t first = boundary > overlap ? boundary - overlap : 0;
  time_t first_session_minute = session->started_at;
  if (first_session_minute % minute) {
    first_session_minute += minute - first_session_minute % minute;
  }
  return (InactivityHistory){
    .next_minute = later(first, first_session_minute),
    .end = end,
    .recent_after = recent_after,
    .detected_at = session->detected_at,
  };
}

void inactivity_history_add(InactivityHistory *history, const InactivitySettings *settings, int steps) {
  if (history->window_size == settings->window_minutes) {
    const int oldest = history->steps[history->window_index];
    if (oldest == ACTIVITY_UNKNOWN_STEPS) {
      history->unknown_minutes--;
    } else {
      history->window_steps -= oldest;
    }
  } else {
    history->window_size++;
  }
  history->steps[history->window_index] = steps;
  history->window_index = (history->window_index + 1) % settings->window_minutes;
  if (steps == ACTIVITY_UNKNOWN_STEPS) {
    history->unknown_minutes++;
  } else {
    history->window_steps += steps;
  }

  history->next_minute += ACTIVITY_MINUTE_SECONDS;
  if (history->window_size < settings->window_minutes) {
    return;
  }
  const time_t window_end = history->next_minute;
  if (window_end > history->recent_after) {
    history->has_recent_window = true;
    history->has_recent_gap |= history->unknown_minutes > 0;
  }
  if (!history->unknown_minutes && history->window_steps >= settings->step_threshold) {
    history->detected_at = later(history->detected_at, window_end);
  }
}

InactivityDecision inactivity_evaluate(const InactivitySettings *settings,
                                       const InactivitySession *session,
                                       const InactivityHistory *history, time_t now) {
  const time_t last_activity = later(session->acknowledged_at, history->detected_at);
  const time_t baseline = later(session->started_at, later(last_activity, session->reminded_at));
  const time_t due = baseline + settings->interval_seconds;
  InactivityDecision decision = {.last_activity_at = last_activity, .next_check_at = due};
  if (now < due) {
    return decision;
  }

  if (!history->has_recent_window || history->has_recent_gap) {
    HistoryRetry retry = session->history_retry;
    if (!retry.interval_seconds || now < retry.started_at) {
      retry = (HistoryRetry){.started_at = now, .interval_seconds = ACTIVITY_MINUTE_SECONDS};
    } else {
      retry.interval_seconds *= 2;
      if (retry.interval_seconds > HISTORY_RETRY_MAX_SECONDS) {
        retry.interval_seconds = HISTORY_RETRY_MAX_SECONDS;
      }
    }
    const time_t deadline = retry.started_at + HISTORY_TIMEOUT_SECONDS;
    if (now >= deadline) {
      decision.stop_monitoring = true;
      decision.next_check_at = 0;
    } else {
      decision.history_retry = retry;
      const time_t next_retry = now + retry.interval_seconds;
      decision.next_check_at = next_retry < deadline ? next_retry : deadline;
    }
  } else {
    decision.request_reminder = true;
    decision.next_check_at = now + settings->interval_seconds;
  }
  return decision;
}
