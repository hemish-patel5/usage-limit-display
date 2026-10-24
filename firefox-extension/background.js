
const RUST_URL = "http://127.0.0.1:8766/images/update";
const CHATGPT_URL =
  "https://chatgpt.com/backend-api/conversation/init";

const INIT_BODY = {
  gizmo_id: null,
  requested_default_model: null,
  conversation_id: null,
  timezone_offset_min: new Date().getTimezoneOffset()
};

function extractQuota(data) {
  const item = data?.limits_progress?.find(
    f => f.feature_name === "image_gen"
  );

  if (
    !item ||
    !Number.isInteger(item.remaining) ||
    item.remaining < 0 ||
    typeof item.reset_after !== "string" ||
    Number.isNaN(Date.parse(item.reset_after))
  ) {
    throw new Error("Valid image_gen quota not found");
  }

  return {
    remaining: item.remaining,
    reset_after: item.reset_after
  };
}

async function fetchDirect() {
  const sessionResponse = await fetch(
    "https://chatgpt.com/api/auth/session",
    {
      credentials: "include",
      cache: "no-store"
    }
  );

  if (!sessionResponse.ok) {
    throw new Error("Session request failed");
  }

  const session = await sessionResponse.json();

  if (!session.accessToken) {
    throw new Error("No ChatGPT session available");
  }

  const headers = {
    "Content-Type": "application/json",
    "Authorization": `Bearer ${session.accessToken}`
  };

  if (session.account?.id) {
    headers["ChatGPT-Account-Id"] = session.account.id;
  }

  const response = await fetch(CHATGPT_URL, {
    method: "POST",
    credentials: "include",
    cache: "no-store",
    headers,
    body: JSON.stringify(INIT_BODY)
  });

  if (!response.ok) {
    throw new Error(`ChatGPT HTTP ${response.status}`);
  }

  return extractQuota(await response.json());
}

async function fetchThroughTab() {
  const tabs = await browser.tabs.query({
    url: "https://chatgpt.com/*"
  });

  for (const tab of tabs) {
    try {
      const result = await browser.tabs.sendMessage(
        tab.id,
        { type: "FETCH_IMAGE_QUOTA" }
      );

      if (result?.ok) {
        return result.quota;
      }
    } catch (error) {
      console.warn("Tab fallback failed:", error);
    }
  }

  throw new Error(
    "Background fetch failed. Open ChatGPT in a tab and retry."
  );
}

async function fetchQuota() {
  try {
    return await fetchDirect();
  } catch (error) {
    console.warn("Direct request failed:", error);
    return fetchThroughTab();
  }
}

let refreshing = null;

async function refresh() {
  if (refreshing) return refreshing;

  refreshing = (async () => {
    try {
      const { rustToken } =
        await browser.storage.local.get("rustToken");

      if (!rustToken) {
        throw new Error("Configure your Rust token first");
      }

      const quota = await fetchQuota();

      const response = await fetch(RUST_URL, {
        method: "POST",
        headers: {
          "Content-Type": "application/json",
          "x-tracker-token": rustToken
        },
        body: JSON.stringify(quota)
      });

      if (!response.ok) {
        throw new Error(
          `Rust server returned HTTP ${response.status}`
        );
      }

      const snapshot = {
        ...quota,
        updated_at: Date.now()
      };

      await browser.storage.local.set({
        snapshot,
        lastError: null
      });

      console.log("Image quota updated:", snapshot);

      return { ok: true, snapshot };

    } catch (error) {
      await browser.storage.local.set({
        lastError: error.message
      });

      console.error("Image refresh failed:", error);

      return { ok: false, error: error.message };
    }
  })();

  try {
    return await refreshing;
  } finally {
    refreshing = null;
  }
}

browser.alarms.create("refresh-images", {
  periodInMinutes: 5
});

browser.alarms.onAlarm.addListener(alarm => {
  if (alarm.name === "refresh-images") {
    refresh();
  }
});

browser.runtime.onMessage.addListener(async message => {
  if (message.type === "REFRESH") {
    return refresh();
  }

  if (message.type === "SAVE_TOKEN") {
    if (
      typeof message.token !== "string" ||
      !message.token.trim()
    ) {
      return { ok: false, error: "Invalid token" };
    }

    await browser.storage.local.set({
      rustToken: message.token.trim()
    });

    return refresh();
  }
});
