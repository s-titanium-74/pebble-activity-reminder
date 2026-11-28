# Pebble Activity Reminder

Configurable inactivity reminders for Pebble Time 2, using activity history and scheduled background checks.

## Status

**Activity Reminder** provides manual monitoring controls and configurable inactivity reminders on Pebble Time 2.

## Pebble Time 2 verification

The [Wakeup spike](https://github.com/s-titanium-74/pebble-activity-reminder/issues/1) reproduced foreground interruption. The physical-watch Background Worker trial preserved the active app, so Background Worker is the selected approach for [inactivity detection and recheck scheduling](https://github.com/s-titanium-74/pebble-activity-reminder/issues/3).

### Build and install

Install the host dependencies from the [Pebble SDK setup guide](https://developer.repebble.com/sdk/), then run these commands from the repository root. The reference toolchain is Pebble CLI 5.3.0 with SDK 4.44. The Emery emulator reports firmware 4.44.3.

```sh
uv tool install 'pebble-tool==5.3.0'
export PATH="$(uv tool dir --bin):$PATH"
pebble --version
pebble sdk install 4.44
pebble sdk activate 4.44
pebble build
pebble install --emulator emery --logs
```

`pebble --version` should report `Pebble Tool v5.3.0`. An `ACME server` message means the shell picked up the unrelated ACME test server. Check `type -a pebble` and use the executable installed by `uv`.

The build produces `build/pebble-activity-reminder.pbw`. On a paired Pebble Time 2, enable Developer Connection in the Pebble mobile app, sign in with the same Pebble account on the computer, and run `pebble install --cloudpebble --logs` after `pebble login`. For a LAN connection, use `pebble install --phone <server-ip> --logs`. Keep the log stream open during each trial. [Developer Connection guide](https://developer.repebble.com/guides/tools-and-resources/developer-connection/)

### Background Worker trial

The earlier 10-second trial polled on a fixed interval. Current builds use rolling Pebble Health history for monitoring. PebbleOS permits one background worker at a time and may prompt to replace another app's worker. [Background Worker guide](https://developer.repebble.com/guides/events-and-services/background-worker/)

On the Emery emulator (firmware 4.44.3), 10-second checks fired on time and read a minute of Health data. The watchface remained visible, and a separate counter app kept its value and accepted button input after two checks. Selecting 30 minutes replaced the pending 10-second timer with a 1,800-second timer. On the physical Pebble Time 2, the user observed no foreground interruption during the Background Worker trial. The physical-watch reminder test below subsequently verified a 30-minute check.

### Extended use

The user reported approximately three days of real-device use without long-running problems. Monitoring after watch restarts and app exits worked, stopping worked, and no duplicate notifications, retry problems, or problems with missing or delayed history were observed. The user also reported no problems with the clock recovery change described below.

Battery life was acceptable, with no perceptible increase in drain. This is a subjective observation, not a controlled enabled-versus-disabled measurement. The firmware, settings, reminder count, and battery readings for this extended run were not recorded. These observations do not establish that scheduling failures or missing history were deliberately induced.

### Activity history trial

[Issue #2](https://github.com/s-titanium-74/pebble-activity-reminder/issues/2) compares recorded steps and VMC (Vector Magnitude Counts) for candidate detection rules. SDK 4.44 exposes both in `HealthMinuteData` through `health_service_get_minute_history()`. The app declares the `health` capability. Enable Pebble Health and allow the app's Health access if the watch requests it. The worker logs the firmware version at start. [Pebble Health API guide](https://developer.repebble.com/guides/events-and-services/health/)

The trial polled the four latest completed minutes every 10 seconds to observe late or revised records. It sampled sitting, walking, light exercise, and the watch sitting unworn.

Each `Health minute` line identifies the minute's start and observation time as Unix timestamps in seconds. `status=valid` includes `steps` and `vmc`, `status=invalid` means the API flagged the record, and `status=missing` means no record was returned for that exact minute. Estimate availability delay by subtracting the minute's end (`minute + 60`) from the first `observed` time with a valid record. Report this as an observed delay at 10-second polling resolution and track any later value changes. A valid zero is a measurement. Missing or invalid minutes leave activity unknown, and the history API does not expose whether the watch was worn for a minute.

Each `Activity window` line evaluates two consecutive completed minutes. The trial uses **20 total steps**. `qualifying` marks the end timestamp at which the inactivity timer would reset. `below_threshold` indicates that both minutes were valid but their total fell below 20. `unknown` means at least one minute was missing or invalid. Activity Spike logs these decisions and keeps its diagnostic check interval independent of them.

On the physical Pebble Time 2, a trial poll classified consecutive window totals of 12 and zero steps as `below_threshold` and 80 steps as `qualifying`. The qualifying window ended six seconds before the poll.

The Emery emulator (firmware 4.44.3) logged four minute positions per poll, including missing minutes and valid records with zero steps. One newly completed minute was visible on the first poll after its end, one second later.

In a physical-watch capture taken while seated, the latest three completed minute records each showed zero steps and VMC values of 1,290, 1,217, and 369. An earlier minute in the same lookback showed 26 steps and VMC 5,921. The newest minute was already valid at the first poll one second after it ended. This bounds the observed delay for that record only.

In a physical-watch capture taken during light stretching, four consecutive valid minutes showed 25, 42, 55, and 30 recorded steps, with VMC values of 319, 263, 0, and 0. Every adjacent two-minute step total exceeded all three trial thresholds, while the VMC values were lower than those in the recent zero-step seated records.

In a capture taken during light cooking, five distinct valid minutes showed step counts of 24, 0, 37, 0, and 17, with VMC values of 111, 0, 0, 577, and 121. The four adjacent two-minute totals were 24, 37, 37, and 17 steps. All four met the 5- and 10-step thresholds, and three met the 20-step threshold. The newest minute was already valid when first queried in the supplied capture, two seconds after it ended.

In a capture taken during light walking, four valid minutes showed step counts of 0, 12, 0, and 29, with VMC values of 242, 0, 219, and 1,462. The adjacent two-minute totals were 12, 12, and 29 steps. All three met the 5- and 10-step thresholds, and one met the 20-step threshold.

In a capture from a Ring Fit Adventure session, the 30 minutes that completed after logging began were all valid at their first observed poll, zero to ten seconds after minute end. Of the 29 adjacent two-minute windows, 27 met each trial step threshold. The two misses totaled four steps and zero steps, and qualifying window end times were never more than two minutes apart within the sample. Five individual minutes recorded zero steps with VMC from 849 to 1,720, overlapping the seated sample. One minute's VMC changed from 327 to 32 between polls while its step count stayed at 30. The log did not identify individual exercises or breaks, so this supports the step rule across the session and leaves exercise-specific detection open.

In the capture taken with the watch off the wrist, the worker continued checking on time. Two consecutive polls covered five distinct completed minutes, all of which returned `status=missing`. The removal time was unrecorded. This sample shows that missing history can coincide with the watch being off the wrist. The cause of a missing record remains unknown to the app.

The activity start and end times were unrecorded, so the sampled minutes could include other activity. Use captures with known phase times to evaluate threshold accuracy. A seated wrist-only sample is still pending.

The provisional rule is **20 steps in two consecutive valid minutes**. This makes missed movements easier to notice while testing. The earlier comparisons used 5, 10, and 20 total steps. A qualifying window contributes its end timestamp as the last activity time, at one-minute resolution. Compare VMC across the same scenarios before proposing a VMC threshold, especially seated wrist movement versus light exercise.

A rule that qualifies either 10 total steps or a two-minute VMC sum above 1,000 recovered both missed Ring Fit windows (29 of 29 instead of 27 of 29). It also qualified both recent zero-step seated windows. Using a VMC value above 1,000 in either minute gave the same counts for the supplied samples. Light walking, cooking, and stretching already met the step rule in every sampled window. The observed trade-off is that the VMC threshold would reset the inactivity timer in the seated sample.

### Wakeup comparison

The earlier trial used Alloy to reserve an initial Wakeup and persist a marker. A scheduled C entry point read the marker and reserved a second Wakeup without creating a window. The initial Alloy-only attempt with no window exited before JavaScript ran in the Emery emulator. [PebbleOS Alloy startup](https://github.com/coredevices/PebbleOS/blob/main/pebble/applib/moddable/moddable.c) · [Wakeup API](https://developer.repebble.com/docs/c/Foundation/Wakeup/)

On a physical Pebble Time 2 with firmware 4.47.2, one 10-second trial covered the watchface for the first Wakeup and Timer Core for the second. The first fired 11 seconds after arming, one second past its requested time. The watchface visibly shifted for a moment. The logs show that Multifunctional 7 Segment Reborn exited and initialized again. The second fired 30 seconds after the first, 20 seconds past its requested time. Timer Core closed when that Wakeup fired. Both checks read the same stored marker and exited through the no-window path. The screen shown after Timer Core closed and its state after reopening remain unrecorded.

With the 10-second interval on Emery firmware 4.44.3, the first Wakeup fired on time. The second reservation succeeded but fired 20 seconds past its requested time, yielding a 30-second gap between launches. [PebbleOS Wakeup service](https://github.com/coredevices/PebbleOS/blob/main/pebble/services/wakeup/service.c)

On the Emery emulator (firmware 4.44.3), both Wakeups in each trial with the original 120-second interval fired at their requested times to the second, read the same stored marker, and exited through the no-window path. The watchface was visible after each exit. Screen samples during a repeat Wakeup showed only the watchface's normal rotation of help text. In the foreground-app trial, both Wakeups terminated the separate counter app and returned to the launcher. Reopening the app after the first Wakeup reset the counter from three to zero. No reservation errors were observed in these trials.

Wakeup failed both acceptance conditions on the physical watch. It caused a visible change on the watchface and interrupted another app.

## Goal

Encourage movement after a configurable period without qualifying activity. Reminders should respond to activity history rather than fire unconditionally on a fixed schedule.

Activity means movement that meets the configured detection criteria, not simply taking a break from work. This is not a medical device and does not detect or diagnose neck stiffness or other symptoms.

## Core requirements

- Target Pebble Time 2.
- Start and stop monitoring manually.
- Make the inactivity interval configurable; 30 minutes is an example, not a finalized default.
- Make activity detection configurable, including the detection method and its relevant thresholds and evaluation window.
- Recheck activity history at every scheduled timer callback before deciding whether to alert.
- Display whole minutes since the last qualifying activity on the reminder screen.
- Provide manual activity acknowledgment and a Stop action on that screen.
- Let the wearer select the reminder vibration pattern.
- Do not include snooze or a separate reminder-suppression feature.
- Use the same configured interval for inactivity detection and post-reminder rechecks.
- Do not include automatic weekday or time-of-day scheduling in the initial version.

## Scheduling

Let T be the configured inactivity interval and L the latest qualifying activity time, considering both detected and manually acknowledged activity.

1. At each scheduled check, read the relevant activity history and resolve L.
2. If the inactivity interval has not elapsed, schedule the next check at L + T.
3. Otherwise, issue a reminder and schedule the next recheck T after that reminder.
4. Re-evaluate history at every check before issuing a reminder.
5. Issuing or dismissing a reminder does not count as activity.

Movement between checks is evaluated from the recorded history at the next check.

The first reminder is due no sooner than T after monitoring starts. The worker uses only completed minutes that started at or after the monitoring start. If neither history nor manual acknowledgment has supplied a qualifying time, the start time sets the first deadline and the latest activity time stays unset (`last_activity=0` in logs). The worker keeps the latest confirmed timestamp for the running session. Changing the interval while monitoring keeps that session and re-evaluates the deadline. Stopping ends the session.

If the watch clock moves behind the current monitoring state or a check is more than an hour past its recorded deadline, the worker starts a fresh session at the current watch time. The next check is one configured interval later. This covers the clock correction that can happen when the watch reconnects to the phone after a restart.

At every check, the worker rereads the minutes since the previous check and at least the most recent T, with overlap for rolling windows. Once a reminder is due, a missing, invalid, or delayed record in any relevant window leaves inactivity unknown. The worker logs `history_pending` and retries after 1, 2, 4, and 8 minutes, then at most every 15 minutes. Normal scheduling resumes once the missing data arrives or falls outside the relevant windows. A valid zero-step record counts as evidence. The current, unfinished minute is outside the evaluation.

If inactivity still cannot be determined one hour after the first pending check, monitoring pauses and all pending timers are canceled. The home screen shows **Monitoring paused** and **Health data unavailable**. Choose **Start** to begin a new session.

The worker logs `Reminder requested` and reserves the next check before requesting the screen. The app logs `Reminder displayed` when it consumes the request. A timer registration failure is logged and retried once at the next minute tick. Restart monitoring to recover from a second failure.

Run the scheduling tests from the repository root with a host C compiler:

```sh
cc -std=c11 -Wall -Wextra -Werror tests/inactivity_test.c worker_src/c/inactivity.c -o /tmp/pebble-inactivity-test
/tmp/pebble-inactivity-test
```

### Example: 30-minute interval

| Time | Behavior |
| --- | --- |
| 12:00 | Last activity was at 11:40. Schedule 12:10. |
| 12:05 | User moves; the app does not need to launch immediately. |
| 12:10 | History shows activity at 12:05. Do not alert; schedule 12:35. |
| 12:35 | If no later activity exists, alert and schedule 13:05. |
| 13:05 | If still inactive, alert again and schedule 13:35. |

## Monitoring controls and reminders

Open **Activity Reminder** from the watch launcher. The home screen shows whether monitoring is on, off, or paused. Press Select to **Start** monitoring with the saved interval or **Stop** an active session. Press Down to open **Settings**, or Back to exit the app. The home and reminder screens list the Select and Down buttons before their actions.

At a due reminder, the watch plays the saved vibration pattern and opens **Time to move**. The screen shows the whole minutes since the most recent qualifying activity from Pebble Health or **I moved**, calculated when the reminder opens. If the session has no qualifying activity, it shows **No activity recorded**.

| Button | Action |
| --- | --- |
| Select | **I moved** records the press time and schedules the next check one interval later. |
| Down | **Stop** stops monitoring and cancels its pending check immediately. |
| Back | **Dismiss** closes the screen and keeps the last activity time and scheduled check. |

The screen also dismisses itself 30 seconds after it appears. If the app was already open, dismissal restores its previous screen. If the worker opened the app, dismissal returns control to PebbleOS. A due reminder can interrupt another watch app because it brings Activity Reminder to the foreground.

On the Emery emulator (firmware 4.44.3), a reminder interrupted a separate counter app. After 30 seconds the reminder closed to the app launcher, and reopening the counter app showed that it had reset. A reminder shown over the monitoring home screen restored that screen when dismissed.

On a physical Pebble Time 2 (firmware 4.47.2), the first 30-minute check ran on time and used a qualifying activity time of 06:38 to defer the reminder until 07:08. The watch showed 06:38 and gave one short vibration. Select acknowledged activity at 07:08:09 and scheduled the next check for 07:38:09. That check found newer activity at 07:19. The resulting 07:49 reminder interrupted the Weather app, and Back returned to the app launcher.

A separate physical-watch run used a four-minute test interval and live Pebble Health data. The first reminder closed after 30 seconds and the next arrived on schedule with the same activity state. Down stopped the worker, and the next scheduled check passed without a reminder. The watch was then restored to the 30-minute build.

### Test the reminder on Pebble Time 2

1. In **Settings**, save a 30-minute interval and **Short**. Start monitoring from home and confirm **Monitoring on** and a `Worker scheduled` line for 1,800 seconds. Walk long enough to qualify during the session and verify a check uses the detected activity time to defer the reminder.
2. With the watch worn and still, wait for a reminder. Verify one short vibration and compare the displayed whole minutes with the elapsed time since `last_activity` in the logs. Press Select and verify the next `Worker scheduled` time is 1,800 seconds after `Activity acknowledged`.
3. Dismiss a subsequent reminder with Back, then let another time out. In each case check that the same last activity time and the reserved recheck survive dismissal.
4. At a later reminder, press Down and verify the worker stops and no further reminders arrive. Reopen the app and confirm **Monitoring off**. Test notification entry from the watchface and from another app with a recognizable state, and record the screen and app state after dismissal.

## Settings

Open **Activity Reminder** and press Down for **Settings**. Use Up or Down to highlight a row and Select to edit it. For numeric fields, Up and Down change the highlighted digit, and Select advances to the next digit or accepts the last one. On the vibration screen, Up and Down choose a pattern and Select accepts it. Back in an editor cancels that field edit. Select **Save** in the settings list to apply the full set, or press Back to discard the draft.

| Setting | Provisional default | Options |
| --- | --- | --- |
| Interval | 30 min | 2–60 min in one-minute steps. Also sets the recheck interval after a reminder. |
| Window | 2 min | 1 minute up to the configured interval. Uses consecutive, complete, valid Health minutes. |
| Step threshold | 20 steps | 1 through 255 × window length in minutes. A total at or above the threshold qualifies. |
| Vibration | Short | **Short** gives one brief pulse, **Double** gives two brief pulses, and **Long** gives one extended pulse. |

Activity detection uses the total step count across the evaluation window. The watch validates all fields before saving. An invalid value leaves the draft open and the previously saved settings in use. Settings are stored on the watch across app exits and reboots. The background worker reads them on startup and at scheduled checks. Reminders use the saved Pebble SDK vibration pattern. [Pebble SDK Vibes](https://developer.repebble.com/docs/c/User_Interface/Vibes/)

Saving while stopped or paused leaves monitoring in that state. The next manual start uses the saved values. When detection settings change during a session, the worker cancels the pending check and re-evaluates using the saved settings. An interval change keeps the last detected activity. A window or step threshold change discards activity detected under the old rule and rereads the new interval plus the minutes required for overlapping windows, limited to the current session. Monitoring start, manual acknowledgment, and the last reminder time still contribute to scheduling. If the new deadline has passed, the worker immediately requests a reminder when history is sufficient or retries when data is missing. A vibration-only change leaves the scheduled time in place and applies when the next reminder is displayed.

On the Emery emulator (firmware 4.44.3), settings survived closing and reopening the app and restarting the emulator. The settings screen rejected a zero-minute interval, changing only vibration left the pending check in place, and shortening the interval triggered an immediate history check. A separate emulator run with synthetic Health history verified an immediate reminder after shortening the interval, the selected pattern on a scheduled app launch, and saving new settings while monitoring was paused.

### Verify settings on Pebble Time 2

1. With monitoring off, change the interval and pattern, save, then close and reopen the app. Check that the saved values reappear and monitoring stays off. Try saving a window longer than the interval and confirm the old values survive.
2. Start monitoring with the saved values. Change the interval during monitoring and verify the new deadline in the worker log. Check that a vibration-only save leaves the pending deadline in place.
3. With the watch worn, set a short interval and use a high threshold for the trial so small movements do not qualify. Let a normal reminder fire for each of **Short**, **Double**, and **Long**. Record the firmware and the physical pulses, including one reminder after an app exit. Restore the desired interval and threshold after the trial.

On a Pebble Time 2 running firmware 4.47.2, a reminder arrived with the interval set to 20 minutes. Changing the interval to two minutes during monitoring replaced the pending check. The next reminder appeared two minutes after the previous one, and **I moved** scheduled the next check two minutes after the acknowledgment. The reminder logs recorded the **Double** and **Short** selections in this run.

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

Initial monitoring uses the steps rule selected in [Issue #2](https://github.com/s-titanium-74/pebble-activity-reminder/issues/2), with the end of the latest qualifying window as the detected activity time.

## Execution strategy

Use a native Background Worker for routine checks and Alloy for the foreground controls. Keep one timer pending and calculate its next due time from the configured inactivity interval and the relevant activity or reminder timestamp.

```text
Worker timer callback
  -> Load settings and activity history
  -> Determine last qualifying activity
  -> Alert only if due
  -> Schedule the next worker timer
```

Routine checks use recorded Pebble Health history and run while the worker remains active. PebbleOS allows one active worker at a time.

## Open decisions and validation

- Tuning the provisional inactivity interval, step threshold, and evaluation window with controlled real-device captures.
- End-to-end reminder behavior on Pebble Time 2, including what happens to another running app when a reminder opens.
- Physical-watch verification of the selectable vibration patterns.
- Worker timer failures and delivery accuracy at the configured intervals.
- Controlled recovery tests for scheduling failures and missing or delayed history.
- Quantitative battery comparison under comparable monitoring-enabled and disabled conditions.

## Prototype plan

1. Compare Wakeup and Background Worker on Pebble Time 2. Background Worker was selected after the physical-watch foreground test.
2. Read activity history and log timestamps, data validity, and candidate activity values.
3. Implement the configurable scheduling algorithm.
4. Add manual monitoring controls and reminders showing the last activity time, I moved, and Stop.
5. Add configurable detection and alert behavior.
6. Test activity between checks, repeated reminders, manual acknowledgment, stopping, and missing history.
7. Measure battery usage and tune detection using observed data.
