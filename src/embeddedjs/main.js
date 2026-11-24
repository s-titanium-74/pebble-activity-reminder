import Poco from "commodetto/Poco";
import Button from "pebble/button";
import WakeUp from "pebble/wakeup";
import Timer from "timer";

const diagnosticChoices = [
  { label: "10 sec (quick test)", seconds: 10 },
  { label: "15 minutes", seconds: 900 },
  { label: "30 minutes", seconds: 1800 },
  { label: "60 minutes", seconds: 3600 },
  { label: "Stop worker", action: stopWorker },
  { label: "Wakeup comparison", action: startWakeupComparison },
];
const noReminder = 0xffffffff;
const launchedForReminder = Boolean(Natives.worker_launched_reminder());
const defaultInterval = Natives.worker_default_interval();
const diagnosticInterval = diagnosticChoices[0].seconds;
let view = "home";
let previousView = "home";
let selection = 0;
let status = "";
let operation;
let reminderTimer;
let elapsedActivityMinutes;
let interval = Natives.worker_active_interval();
let paused = Boolean(Natives.worker_monitoring_paused());

const render = new Poco(screen);
const heading = new render.Font("Gothic-Bold", 24);
const large = new render.Font("Gothic-Bold", 28);
const body = new render.Font("Gothic-Regular", 18);
const white = render.makeColor(255, 255, 255);
const black = render.makeColor(0, 0, 0);
const blue = render.makeColor(0, 85, 170);
const lightGray = render.makeColor(221, 221, 221);
const darkGray = render.makeColor(85, 85, 85);

function drawAction(label, button, y, primary) {
  render.fillRectangle(primary ? blue : lightGray, 8, y, render.width - 16, 32);
  const color = primary ? white : black;
  render.drawText(label, heading, color, 16, y + 1);
  render.drawText(button, body, color, render.width - 56, y + 5);
}

function drawHome() {
  render.drawText("Activity Reminder", heading, black, 12, 8);
  const active = interval > diagnosticInterval;
  const monitoring = paused ? "Monitoring paused" : active ? "Monitoring on" : "Monitoring off";
  render.drawText(monitoring, heading, black, 12, 52);
  if (active) {
    render.drawText(`${interval / 60} min`, body, darkGray, 12, 80);
  } else if (paused || interval === diagnosticInterval) {
    render.drawText(paused ? "Health data unavailable" : "Diagnostics running", body, darkGray, 12, 80);
  }
  drawAction(active ? "Stop" : "Start", "Select", 118, true);
  drawAction("Diagnostics", "Down", 158, false);
  render.drawText(status, body, black, 12, 197);
}

function drawDiagnostics() {
  render.drawText("Diagnostics", heading, black, 8, 6);
  diagnosticChoices.forEach(({ label }, index) => {
    const y = 38 + index * 25;
    if (index === selection) {
      render.fillRectangle(blue, 0, y, render.width, 25);
    }
    render.drawText(label, body, index === selection ? white : black, 8, y + 2);
  });
  render.drawText(status, body, black, 8, 198);
}

function drawReminder() {
  render.drawText("Time to move", heading, black, 12, 8);
  if (elapsedActivityMinutes !== undefined) {
    render.drawText(`${elapsedActivityMinutes} min`, large, black, 12, 39);
    render.drawText("since last activity", body, darkGray, 12, 72);
  } else {
    render.drawText("No activity recorded", heading, black, 12, 52);
  }
  drawAction("I moved", "Select", 111, true);
  drawAction("Stop", "Down", 151, false);
  render.drawText(status || "Back  Dismiss", body, darkGray, 12, 197);
}

function draw() {
  render.begin();
  render.fillRectangle(white, 0, 0, render.width, render.height);
  if (view === "reminder") {
    drawReminder();
  } else if (view === "diagnostics") {
    drawDiagnostics();
  } else {
    drawHome();
  }
  render.end();
}

function showReminder(activity) {
  if (view !== "reminder") {
    previousView = view;
  }
  view = "reminder";
  status = "";
  elapsedActivityMinutes = activity ? Natives.worker_elapsed_activity_minutes(activity) : undefined;
  Timer.clear(reminderTimer);
  reminderTimer = Timer.set(dismissReminder, 30000);
  draw();
}

function dismissReminder() {
  Timer.clear(reminderTimer);
  reminderTimer = undefined;
  if (launchedForReminder) {
    watch.exit();
    return;
  }
  view = previousView;
  status = "";
  draw();
}

function startWorker(seconds, exitWhenStarted = false) {
  const result = Natives.worker_start(seconds);
  if (result === 0 || result === 5) {
    status = result === 5 ? "Confirm replacement" : "Starting...";
    operation = { kind: "start", seconds, exitWhenStarted, deadline: Date.now() + 20000 };
  } else {
    status = "Could not start";
  }
  console.log(`Worker start returned ${result}`);
}

function stopWorker() {
  const result = Natives.worker_stop();
  if (result === 0) {
    status = "Stopping...";
    operation = { kind: "stop", deadline: Date.now() + 3000 };
  } else {
    status = "Could not stop";
  }
  console.log(`Worker stop returned ${result}`);
}

function acknowledgeActivity() {
  const result = Natives.worker_acknowledge_activity();
  if (result === 0) {
    status = "Recording...";
    operation = { kind: "acknowledge", deadline: Date.now() + 3000 };
  } else {
    status = "Could not record";
    console.log(`Worker acknowledge returned ${result}`);
  }
}

function refresh() {
  const nextInterval = Natives.worker_active_interval();
  const nextPaused = Boolean(Natives.worker_monitoring_paused());
  let redraw = nextInterval !== interval || nextPaused !== paused;
  interval = nextInterval;
  paused = nextPaused;

  if (operation) {
    const finished = operation.kind === "start" ? interval === operation.seconds :
      operation.kind === "stop" ? !Natives.worker_is_running() :
      Natives.worker_acknowledgment_received();
    if (finished) {
      const completed = operation;
      operation = undefined;
      status = "";
      if (completed.exitWhenStarted && view === "diagnostics") {
        watch.exit();
        return;
      }
      if (completed.kind !== "start" && view === "reminder") {
        dismissReminder();
        return;
      }
      redraw = true;
    } else if (Date.now() >= operation.deadline) {
      status = operation.kind === "start" ? "Start not confirmed" :
        operation.kind === "stop" ? "Stop not confirmed" : "Activity not confirmed";
      operation = undefined;
      redraw = true;
    }
  }

  if (view === "reminder" && !operation && interval <= diagnosticInterval && !status) {
    dismissReminder();
    return;
  }
  const activity = Natives.worker_take_reminder();
  if (activity !== noReminder) {
    showReminder(activity);
  } else if (redraw) {
    draw();
  }
}

function startWakeupComparison() {
  if (Natives.worker_active_interval()) {
    status = "Stop the worker first";
    return;
  }
  const seconds = 10;
  const cookie = 1;

  try {
    WakeUp.cancel(); // Clear only this app's outstanding reservations.
    const startedAt = Math.floor(Date.now() / 1000);
    const dueAt = startedAt + seconds;
    const id = WakeUp.schedule(dueAt * 1000, cookie, false);
    const result = Natives.save_initial_wakeup(id, dueAt, startedAt, seconds);
    if (result !== 0) {
      WakeUp.cancel(id);
      throw new Error(`persist_write_data returned ${result}`);
    }

    const { major, minor, patch } = watch.firmwareVersion;
    console.log(`Wakeup Spike armed id=${id} cookie=${cookie} due=${dueAt} firmware=${major}.${minor}.${patch}`);
    return true;
  } catch (error) {
    status = "Wakeup reservation failed";
    console.log(`Wakeup Spike error: ${error}`);
  }
}

const buttons = new Button({
  types: ["back", "up", "down", "select"],
  onPush(down, button) {
    // A launch from the watch menu can deliver the opening button's release here.
    if (!down) return;
    if (button === "back") {
      if (view === "reminder") {
        dismissReminder();
      } else if (view === "diagnostics") {
        view = "home";
        status = "";
        draw();
      } else {
        watch.exit();
      }
      return;
    }
    if (operation) return;

    if (view === "reminder") {
      if (button === "select") acknowledgeActivity();
      if (button === "down") stopWorker();
    } else if (view === "home") {
      if (button === "select") {
        if (interval > diagnosticInterval) {
          stopWorker();
        } else {
          startWorker(defaultInterval);
        }
      } else if (button === "down") {
        selection = diagnosticChoices.findIndex(choice => choice.seconds === (interval || defaultInterval));
        if (selection < 0) selection = 0;
        view = "diagnostics";
        status = "";
      }
    } else if (button === "select") {
      const choice = diagnosticChoices[selection];
      if (choice.action) {
        if (choice.action()) {
          watch.exit();
          return;
        }
      } else {
        startWorker(choice.seconds, true);
      }
    } else {
      const direction = button === "up" ? -1 : 1;
      selection = (selection + direction + diagnosticChoices.length) % diagnosticChoices.length;
    }
    draw();
  },
});

const initialActivity = Natives.worker_take_reminder();
if (initialActivity !== noReminder) {
  showReminder(initialActivity);
} else if (launchedForReminder) {
  watch.exit();
} else {
  draw();
}
Timer.repeat(refresh, 250);
