import Poco from "commodetto/Poco";
import Button from "pebble/button";
import Timer from "timer";

const settingsFields = [
  { label: "Interval", digits: 2, unit: "min" },
  { label: "Window", digits: 2, unit: "min" },
  { label: "Step threshold", digits: 5, unit: "steps" },
  { label: "Vibration" },
  { label: "Save" },
];
const vibrationPatterns = ["Short", "Double", "Long"];
const settingsErrors = [
  "",
  "Interval must be 2–60 min",
  "Window must fit interval",
  "Steps must be 1–255 x window",
  "Choose a vibration pattern",
];

const noReminder = 0xffffffff;
const launchedForReminder = Boolean(Natives.worker_launched_reminder());
let view = "home";
let previousView = "home";
let selection = 0;
let draft;
let edit;
let settingsStatus = "";
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
const lavender = render.makeColor(170, 170, 255);
const pink = render.makeColor(255, 170, 170);
const plum = render.makeColor(85, 0, 85);
const berry = render.makeColor(170, 0, 85);
const darkGray = render.makeColor(85, 85, 85);

function drawButtonAction(button, label, y) {
  const keyX = 12;
  const keyWidth = 70;
  const textX = keyX + Math.floor((keyWidth - render.getTextWidth(button, heading)) / 2);
  const textY = y + 3;
  render.fillRectangle(pink, keyX, y + 2, keyWidth, 28);
  render.drawText(button, heading, plum, textX, textY);
  render.drawText(label, heading, black, keyX + keyWidth + 14, textY);
}

function drawHome() {
  render.drawText("Activity Reminder", large, plum, 8, 2);
  const active = interval > 0;
  const monitoring = paused ? "Monitoring paused" : active ? "Monitoring on" : "Monitoring off";
  render.drawText(monitoring, heading, black, 12, 52);
  if (active) {
    render.drawText(`${interval / 60} min`, body, darkGray, 12, 80);
  } else if (paused) {
    render.drawText("Health data unavailable", body, darkGray, 12, 80);
  }
  drawButtonAction("Select", active ? "Stop" : "Start", 116);
  drawButtonAction("Down", "Settings", 155);
  render.drawText(status, body, black, 12, 197);
}

function drawSettings() {
  render.drawText("Settings", large, plum, 8, 2);
  const values = [
    `${draft[0]} min`, `${draft[1]} min`, String(draft[2]), vibrationPatterns[draft[3]],
  ];
  const rowHeight = 32;
  settingsFields.forEach(({ label }, index) => {
    const y = 36 + index * rowHeight;
    const textY = y + 3;
    const selected = index === selection;
    const color = selected ? white : black;
    if (selected) {
      render.fillRectangle(berry, 0, y, render.width, rowHeight);
    }
    render.drawText(label, heading, color, 8, textY);
    if (values[index]) {
      const x = render.width - 8 - render.getTextWidth(values[index], heading);
      render.drawText(values[index], heading, color, x, textY);
    }
  });
  if (settingsStatus) {
    render.drawText(settingsStatus, body, darkGray, 8, 198);
  }
}

function drawSettingEditor() {
  const field = settingsFields[selection];
  render.drawText(field.label, heading, plum, 8, 5);
  if (field.digits) {
    const digits = String(edit.value).padStart(field.digits, "0");
    for (let index = 0; index < field.digits; index++) {
      const x = 12 + index * 23;
      const active = index === edit.digit;
      if (active) {
        render.fillRectangle(berry, x - 2, 57, 21, 34);
      }
      render.drawText(digits[index], large, active ? white : black, x, 60);
    }
    render.drawText(field.unit, body, darkGray, 12, 104);
    render.drawText("Up / Down  Change digit", body, black, 12, 142);
    const action = edit.digit + 1 < field.digits ? "Next digit" : "Done";
    render.drawText(`Select  ${action}`, body, black, 12, 167);
  } else {
    vibrationPatterns.forEach((label, index) => {
      const y = 45 + index * 32;
      const active = index === edit.value;
      if (active) {
        render.fillRectangle(berry, 0, y, render.width, 32);
      }
      render.drawText(label, heading, active ? white : black, 12, y + 3);
    });
    render.drawText("Select  Done", body, black, 12, 167);
  }
}

function openSettings() {
  draft = Array.from({ length: settingsFields.length - 1 }, (_, index) => Natives.worker_settings_value(index));
  selection = 0;
  settingsStatus = "";
  view = "settings";
}

function handleSettingsButton(button) {
  if (button !== "select") {
    const direction = button === "up" ? -1 : 1;
    selection = (selection + direction + settingsFields.length) % settingsFields.length;
    return;
  }
  if (selection < draft.length) {
    edit = { value: draft[selection], digit: 0 };
    view = "settingEditor";
    return;
  }

  const result = Natives.worker_save_settings(...draft);
  if (result === 0) {
    view = "home";
    status = "Settings saved";
  } else {
    settingsStatus = settingsErrors[result] || "Could not save settings";
  }
}

function handleEditorButton(button) {
  const { digits } = settingsFields[selection];
  if (button === "select") {
    if (digits && edit.digit + 1 < digits) {
      edit.digit++;
    } else {
      draft[selection] = edit.value;
      settingsStatus = "";
      view = "settings";
    }
    return;
  }

  const direction = button === "up" ? 1 : -1;
  if (digits) {
    const place = 10 ** (digits - edit.digit - 1);
    const digit = Math.floor(edit.value / place) % 10;
    const nextDigit = (digit + direction + 10) % 10;
    edit.value += (nextDigit - digit) * place;
  } else {
    edit.value = (edit.value - direction + vibrationPatterns.length) % vibrationPatterns.length;
  }
}

function drawReminder() {
  render.drawText("Time to move", heading, plum, 8, 5);
  if (elapsedActivityMinutes !== undefined) {
    render.drawText(`${elapsedActivityMinutes} min`, large, black, 12, 39);
    render.drawText("since last activity", heading, black, 12, 72);
  } else {
    render.drawText("No activity recorded", heading, black, 12, 52);
  }
  drawButtonAction("Select", "I moved", 116);
  drawButtonAction("Down", "Stop", 155);
  render.drawText(status, body, black, 12, 197);
}

function draw() {
  render.begin();
  render.fillRectangle(white, 0, 0, render.width, render.height);
  render.fillRectangle(lavender, 0, 0, render.width, 36);
  if (view === "reminder") {
    drawReminder();
  } else if (view === "settings") {
    drawSettings();
  } else if (view === "settingEditor") {
    drawSettingEditor();
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

function startWorker() {
  const result = Natives.worker_start();
  if (result === 0 || result === 5) {
    status = result === 5 ? "Confirm replacement" : "Starting...";
    operation = { kind: "start", deadline: Date.now() + 20000 };
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
    const finished = operation.kind === "start" ? interval > 0 :
      operation.kind === "stop" ? !Natives.worker_is_running() :
      Natives.worker_acknowledgment_received();
    if (finished) {
      const completed = operation;
      operation = undefined;
      status = "";
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

  if (view === "reminder" && !operation && !interval && !status) {
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

const buttons = new Button({
  types: ["back", "up", "down", "select"],
  onPush(down, button) {
    // A launch from the watch menu can deliver the opening button's release here.
    if (!down) return;
    if (button === "back") {
      if (view === "reminder") {
        dismissReminder();
      } else if (view === "settingEditor") {
        view = "settings";
        draw();
      } else if (view === "settings") {
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
        if (interval) {
          stopWorker();
        } else {
          startWorker();
        }
      } else if (button === "down") {
        openSettings();
      }
    } else if (view === "settings") {
      handleSettingsButton(button);
    } else if (view === "settingEditor") {
      handleEditorButton(button);
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
