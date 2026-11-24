#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

enum {
  ACTIVITY_MINUTE_SECONDS = 60,
  ACTIVITY_DEFAULT_WINDOW = 2,
  ACTIVITY_DEFAULT_STEPS = 20,
  ACTIVITY_MAX_WINDOW = 60,
  ACTIVITY_UNKNOWN_STEPS = -1,
};

typedef struct {
  int32_t interval_seconds;
  int window_minutes;
  int step_threshold;
} InactivitySettings;

typedef struct {
  time_t started_at;
  int interval_seconds;
} HistoryRetry;

typedef struct {
  time_t started_at;
  time_t acknowledged_at;
  time_t detected_at;
  time_t reminded_at;
  time_t history_end;
  HistoryRetry history_retry;
} InactivitySession;

typedef struct {
  time_t next_minute;
  time_t end;
  time_t recent_after;
  time_t detected_at;
  int window_steps;
  int window_size;
  int window_index;
  int unknown_minutes;
  int16_t steps[ACTIVITY_MAX_WINDOW];
  bool has_recent_window;
  bool has_recent_gap;
} InactivityHistory;

typedef struct {
  time_t last_activity_at;
  time_t next_check_at;
  HistoryRetry history_retry;
  bool request_reminder;
  bool stop_monitoring;
} InactivityDecision;

bool inactivity_settings_valid(const InactivitySettings *settings);
InactivityHistory inactivity_history_begin(const InactivitySettings *settings,
                                           const InactivitySession *session, time_t now);
// Feed every minute in order, including missing or invalid minutes as ACTIVITY_UNKNOWN_STEPS.
void inactivity_history_add(InactivityHistory *history, const InactivitySettings *settings, int steps);
InactivityDecision inactivity_evaluate(const InactivitySettings *settings,
                                       const InactivitySession *session,
                                       const InactivityHistory *history, time_t now);
