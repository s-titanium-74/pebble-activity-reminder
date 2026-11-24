# Pebble Activity Reminder

Configurable inactivity reminders for Pebble Time 2, using activity history and scheduled wakeups.

## Status

Requirements and design exploration only. No application implementation is included yet. Proposed APIs, lifecycle behavior, and battery usage must be validated on the target device.

## Goal

Encourage movement after a configurable period without qualifying activity. Reminders should respond to activity history rather than fire unconditionally on a fixed schedule.

Activity means movement that meets the configured detection criteria, not simply taking a break from work. This is not a medical device and does not detect or diagnose neck stiffness or other symptoms.

## Core requirements

- Target Pebble Time 2.
- Start and stop monitoring manually.
- Make the inactivity interval configurable; 30 minutes is an example, not a finalized default.
- Make activity detection configurable, including the detection method and its relevant thresholds and evaluation window.
- Recheck activity history at every scheduled wakeup before deciding whether to alert.
- Display the last qualifying activity time on the reminder screen.
- Provide manual activity acknowledgment and a Stop action on that screen.
- Make vibration intensity/pattern configurable; exact options remain to be defined and validated.
- Do not include snooze or a separate reminder-suppression feature.
- Use the same configured interval for inactivity detection and post-reminder rechecks.
- Do not include automatic weekday or time-of-day scheduling in the initial version.

## Scheduling

Let T be the configured inactivity interval and L the latest qualifying activity time, considering both detected and manually acknowledged activity.

1. On wakeup, read the relevant activity history and resolve L.
2. If the inactivity interval has not elapsed, schedule the next wakeup at L + T and exit.
3. Otherwise, issue a reminder and schedule the next recheck T after that reminder.
4. Always re-evaluate history at the next wakeup. A reservation alone is not a reason to issue a reminder.
5. Issuing or dismissing a reminder does not count as activity.

Movement between wakeups does not require immediately rescheduling the app; the next wakeup evaluates the recorded history.

### Example: 30-minute interval

| Time | Behavior |
| --- | --- |
| 12:00 | Last activity was at 11:40. Schedule 12:10. |
| 12:05 | User moves; the app does not need to launch immediately. |
| 12:10 | History shows activity at 12:05. Do not alert; schedule 12:35. |
| 12:35 | If no later activity exists, alert and schedule 13:05. |
| 13:05 | If still inactive, alert again and schedule 13:35. |

## Reminder screen and actions

Example layout; final wording and button mapping are undecided:

```text
Time to move
Last activity: 11:30

[I moved]
[Stop]
```

- **I moved:** Record the current time as manually acknowledged activity and reschedule for current time + T. When combining manual and detected activity, use the most recent qualifying timestamp.
- **Stop:** Stop monitoring and cancel pending wakeups.
- **Dismiss / no acknowledgment:** Do not change the last activity time or suppress future reminders. Keep the scheduled recheck. Screen lifetime and automatic dismissal behavior remain undecided.

## Settings

| Setting | Purpose |
| --- | --- |
| Inactivity interval | Time since qualifying activity before an alert; also the interval between reminders while inactivity continues. |
| Activity detection method | Select how movement qualifies as activity. |
| Evaluation window | Period over which activity is evaluated, where applicable. |
| Activity thresholds | Method-specific values such as steps within the evaluation window. |
| Alert strength | Configurable vibration behavior; supported options to be validated. |

No detection thresholds or default interval have been finalized. Earlier examples such as 20 steps within two minutes are placeholders, not validated recommendations.

## Activity detection candidates

These are options to investigate, not commitments to implement every method.

| Method | Candidate rule | Trade-off |
| --- | --- | --- |
| Steps | At least N steps within W minutes. | Easy to understand; may miss non-walking movement. |
| Movement amount (VMC) | Movement metric exceeds a threshold within W minutes. | Could cover non-walking activity; wrist-only motion may cause false resets. API availability and useful thresholds need validation. |
| Steps OR movement amount | Either condition qualifies. | Broader detection, potentially more false resets. |
| Steps AND movement amount | Both conditions qualify. | Stricter; benefit over steps alone needs validation. |
| Heart rate | A threshold sustained for a duration. | Measures exertion rather than simply moving; not selected for the initial scope. |

Manual acknowledgment is a required supplementary input, independent of the automatic detection method.

A steps-first prototype has been proposed, but the final initial detection method is not yet decided. For window-based detection, the proposed activity timestamp is the end of the latest qualifying window; this convention still needs confirmation.

## Execution strategy

Wakeup-based execution is the initial candidate, not a finalized implementation decision:

```text
Wakeup launch
  -> Load settings and activity history
  -> Determine last qualifying activity
  -> Alert only if due
  -> Schedule next wakeup
  -> Exit
```

Important constraints and uncertainties:

- Wakeup launches the foreground application; it is not silent background execution.
- On a no-alert path, the proposal is to avoid presenting a Window and exit after scheduling.
- No-window launch behavior, visible transitions, and the destination after exit require device testing.
- Interruption of another foreground app must be tested separately from launching over a watchface.
- Lower battery consumption than a Background Worker is not guaranteed without measurement.
- A Background Worker remains an alternative if foreground disruption is unacceptable; supported APIs, vibration behavior, and coexistence restrictions require investigation.
- Prefer existing recorded activity history over implementing continuous raw-sensor monitoring.

## Open decisions and validation

- Initial automatic detection method, thresholds, evaluation window, and defaults.
- Monitoring-start baseline: how existing activity history affects the first reminder.
- Behavior when history is missing, invalid, delayed, or the watch is not worn. Missing data must not automatically be treated as zero activity.
- Reminder screen lifetime, dismissal behavior, and physical button mapping.
- Available vibration options and their real-device behavior.
- Wakeup scheduling failures, conflicts, and timestamp precision.
- Persistence and recovery across watch restarts.
- Re-evaluation and replacement of pending reservations after settings change.
- Actual foreground transitions and battery consumption.
- Implementation language, SDK setup, and whether any existing project is suitable for reuse. No dependency on Move Now has been selected.

## Prototype plan

1. Validate wakeup launch and no-window exit on a watchface and over another app.
2. Read activity history and log timestamps, data validity, and candidate activity values.
3. Implement the configurable scheduling algorithm.
4. Add reminders showing the last activity time, I moved, and Stop.
5. Add manual monitoring controls and configurable alert behavior.
6. Test activity between wakeups, repeated reminders, manual acknowledgment, stopping, and missing history.
7. Measure battery usage and tune detection using observed data.
