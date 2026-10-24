
const remainingEl = document.getElementById("remaining");
const resetEl = document.getElementById("reset");
const statusEl = document.getElementById("status");

function formatDuration(milliseconds) {
  const seconds = Math.max(
    0,
    Math.floor(milliseconds / 1000)
  );

  const days = Math.floor(seconds / 86400);
  const hours = Math.floor((seconds % 86400) / 3600);
  const minutes = Math.floor((seconds % 3600) / 60);

  if (days > 0) {
    return `${days}d ${hours}h ${minutes}m`;
  }

  return `${hours}h ${minutes}m`;
}

async function display() {
  const { snapshot, lastError } =
    await browser.storage.local.get([
      "snapshot",
      "lastError"
    ]);

  if (snapshot) {
    remainingEl.textContent =
      `${snapshot.remaining} remaining`;

    const resetTime = Date.parse(snapshot.reset_after);

    resetEl.textContent =
      `Reset: ${formatDuration(resetTime - Date.now())}`;

    const ageMinutes = Math.floor(
      (Date.now() - snapshot.updated_at) / 60000
    );

    statusEl.textContent = lastError
      ? `Last update ${ageMinutes}m ago. Error: ${lastError}`
      : `Updated ${ageMinutes}m ago`;
  } else {
    remainingEl.textContent = "-- remaining";
    resetEl.textContent = "Reset: unknown";
    statusEl.textContent = lastError || "No data yet";
  }
}

document.getElementById("refresh").addEventListener(
  "click",
  async () => {
    statusEl.textContent = "Refreshing...";

    await browser.runtime.sendMessage({
      type: "REFRESH"
    });

    await display();
  }
);

document.getElementById("save").addEventListener(
  "click",
  async () => {
    const token = document.getElementById("token").value;

    if (!token.trim()) {
      statusEl.textContent = "Enter your Rust token";
      return;
    }

    statusEl.textContent = "Saving and testing...";

    await browser.runtime.sendMessage({
      type: "SAVE_TOKEN",
      token
    });

    document.getElementById("token").value = "";
    await display();
  }
);

display();
setInterval(display, 10000);
