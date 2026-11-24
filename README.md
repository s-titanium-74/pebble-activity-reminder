# Pebble Activity Reminder

Configurable inactivity reminders for Pebble Time 2, using activity history and scheduled background checks.

## Status

**Activity Reminder** provides manual monitoring controls and inactivity reminders on Pebble Time 2. The watch app also includes diagnostics for Pebble Health history and Wakeup behavior.

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

### Test Background Worker

1. Open **Activity Reminder** from the watch launcher and press Down for **Diagnostics**. Use Up or Down to choose **10 sec (quick test)**, **15 minutes**, **30 minutes**, or **60 minutes**. Press Select to start the worker or replace its pending timer with the selected interval. The first selection defaults to 30 minutes.
2. For a quick test, choose **10 sec (quick test)** and return to the watchface or open another app immediately. Use `Worker scheduled` and `Worker check` to identify the check times. Across at least two checks, observe the display, app state, and button input. Repeat with both the watchface and the other app.
3. Choose **30 minutes**, leave the watch idle with a watchface, and capture the next `Worker check` to verify long-interval delivery. The timestamp and delay are logged in seconds. The `Inactivity check` line shows the latest qualifying activity time, the next scheduled time, and the decision.
4. Reopen **Activity Reminder**, press Down for **Diagnostics**, choose **Stop worker**, and verify the checks cease. For battery measurements, disconnect live logging and use the same watchface and connection conditions with the worker stopped and with the normal interval enabled. PebbleOS permits one background worker at a time and may prompt to replace another app's worker. [Background Worker guide](https://developer.repebble.com/guides/events-and-services/background-worker/)

The Alloy menu controls the native worker. The 10-second diagnostic polls on a fixed interval. The 15-, 30-, and 60-minute options run inactivity monitoring and schedule the next check based on activity history and reminder requests.

On the Emery emulator (firmware 4.44.3), 10-second checks fired on time and read a minute of Health data. The watchface remained visible, and a separate counter app kept its value and accepted button input after two checks. Selecting 30 minutes replaced the pending 10-second timer with a 1,800-second timer. On the physical Pebble Time 2, the user observed no foreground interruption during the Background Worker trial. A full 30-minute check and battery impact remain to be measured on the physical watch.

### Test activity history

[Issue #2](https://github.com/s-titanium-74/pebble-activity-reminder/issues/2) compares recorded steps and VMC (Vector Magnitude Counts) for candidate detection rules. SDK 4.44 exposes both in `HealthMinuteData` through `health_service_get_minute_history()`. The app declares the `health` capability. Enable Pebble Health and allow the app's Health access if the watch requests it. The worker logs the firmware version at start. [Pebble Health API guide](https://developer.repebble.com/guides/events-and-services/health/)

1. Install the build and save the log stream. In the watch app, open **Diagnostics** and select **10 sec (quick test)**. The worker rereads the four latest completed minutes on every poll so a late or revised record can be observed.
2. Note the start and end time of each phase. Give each phase two complete clock minutes and switch phases near minute boundaries. Test sitting still, normal walking, moving the watch wrist while seated, light non-walking exercise such as standing arm movements with feet planted, and leaving the watch still on a table while unworn. Record whether Pebble Health was enabled during the trial.
3. Keep the worker running for at least one additional minute after the last phase to catch late records. Then select **Stop worker**. Use the `Health minute` lines and the phase times for comparison.

```sh
pebble install build/pebble-activity-reminder.pbw --cloudpebble --logs | tee /tmp/pebble-health.log
```

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

### Repeat the Wakeup comparison

1. Record the watch firmware version. Stop the worker, then open **Diagnostics** in **Activity Reminder** and choose **Wakeup comparison**. Confirm the log contains `Wakeup Spike armed`. This replaces any earlier Wakeup reservations made by the spike.
2. Return to the watchface immediately and record until the log shows `Trial complete`. The first Wakeup is requested for 10 seconds after arming. It reads the trial marker and requests a second Wakeup 10 seconds later. In both the emulator and the physical-watch trials, the second fired 30 seconds after the first, so allow about 40 seconds for the full trial.
3. Start a new comparison and open a different watchapp with a recognizable state before the first Wakeup. Note its name and state. Record both Wakeups and check whether the app, its state, and button input remain available without manual recovery. If the first Wakeup interrupts the app, record what happened and reopen it before the second Wakeup.
4. For each Wakeup, record the scheduled and actual times, any visible transition, the screen shown after exit, and any interruption or reservation error. The log reports error codes and delay in seconds. Keep emulator and physical-watch observations distinct.

The comparison uses Alloy to arm a 10-second reservation and store a small trial record through the SDK's native interface. The scheduled path reads that record before entering the Alloy event loop. It returns from the C entry point without creating a `Window`. The initial Alloy-only attempt with no `Window` exited before JavaScript ran in the Emery emulator. The [PebbleOS Alloy startup](https://github.com/coredevices/PebbleOS/blob/main/pebble/applib/moddable/moddable.c) and [app event loop](https://github.com/coredevices/PebbleOS/blob/main/pebble/applib/app.c) show why. The SDK documents a one-minute restriction between pending reservations from the same app. The spike schedules its second reservation after the first fires. [Wakeup API](https://developer.repebble.com/docs/c/Foundation/Wakeup/)

### Findings and decision

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
- Display the last qualifying activity time on the reminder screen.
- Provide manual activity acknowledgment and a Stop action on that screen.
- Make vibration intensity/pattern configurable; exact options remain to be defined and validated.
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

The first reminder is due no sooner than T after monitoring starts. The worker uses only completed minutes that started at or after the monitoring start. If neither history nor manual acknowledgment has supplied a qualifying time, the start time sets the first deadline and the latest activity time stays unset (`last_activity=0` in logs). The worker keeps the latest confirmed timestamp for the running session. Changing the interval while monitoring keeps that session and re-evaluates the deadline. Stopping or switching to the 10-second diagnostic ends the session.

At every check, the worker rereads the minutes since the previous check and at least the most recent T, with overlap for rolling windows. Once a reminder is due, a missing, invalid, or delayed record in any relevant window leaves inactivity unknown. The worker logs `history_pending` and retries after 1, 2, 4, and 8 minutes, then at most every 15 minutes. Normal scheduling resumes once the missing data arrives or falls outside the relevant windows. A valid zero-step record counts as evidence. The current, unfinished minute is outside the evaluation.

If inactivity still cannot be determined one hour after the first pending check, monitoring pauses and all pending timers are canceled. The home screen shows **Monitoring paused** and **Health data unavailable**. Choose **Start** to begin a new session. The **10 sec (quick test)** diagnostic uses its own fixed interval.

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

Open **Activity Reminder** from the watch launcher. The home screen shows whether monitoring is on, off, or paused. Press Select to **Start** a 30-minute monitoring session or **Stop** an active session. Press Down to open **Diagnostics**.

At a due reminder, the watch gives one short vibration and opens **Time to move**. The screen shows the last qualifying activity time in the watch's 12- or 24-hour format, or **No activity recorded** if the session has none.

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

1. Start monitoring from home and confirm **Monitoring on** and a `Worker scheduled` line for 1,800 seconds. Walk long enough to qualify during the session and verify a check uses the detected activity time to defer the reminder.
2. With the watch worn and still, wait for a reminder. Verify one short vibration and compare the displayed time with `last_activity` in the logs. Press Select and verify the next `Worker scheduled` time is 1,800 seconds after `Activity acknowledged`.
3. Dismiss a subsequent reminder with Back, then let another time out. In each case check that the same last activity time and the reserved recheck survive dismissal.
4. At a later reminder, press Down and verify the worker stops and no further reminders arrive. Reopen the app and confirm **Monitoring off**. Test notification entry from the watchface and from another app with a recognizable state, and record the screen and app state after dismissal.

## Settings

| Setting | Purpose |
| --- | --- |
| Inactivity interval | Time since qualifying activity before an alert; also the interval between reminders while inactivity continues. |
| Activity detection method | Select how movement qualifies as activity. |
| Evaluation window | Period over which activity is evaluated, where applicable. |
| Activity thresholds | Method-specific values such as steps within the evaluation window. |
| Alert strength | Configurable vibration behavior; supported options to be validated. |

The trial detection threshold is 20 steps in two minutes. The production threshold and inactivity interval remain configurable design parameters.

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

Initial monitoring uses the steps rule selected in [Issue #2](https://github.com/s-titanium-74/pebble-activity-reminder/issues/2), with the end of the latest qualifying window as the detected activity time. The interval, window length, and step threshold are configurable in the implementation, with provisional values of 30 minutes, two minutes, and 20 steps.

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
- Available vibration options and their real-device behavior.
- Worker timer failures and delivery accuracy at the configured intervals.
- Persistence and recovery across watch restarts.
- Battery consumption at normal monitoring intervals.

## Prototype plan

1. Compare Wakeup and Background Worker on Pebble Time 2. Background Worker was selected after the physical-watch foreground test.
2. Read activity history and log timestamps, data validity, and candidate activity values.
3. Implement the configurable scheduling algorithm.
4. Add manual monitoring controls and reminders showing the last activity time, I moved, and Stop.
5. Add configurable detection and alert behavior.
6. Test activity between checks, repeated reminders, manual acknowledgment, stopping, and missing history.
7. Measure battery usage and tune detection using observed data.
