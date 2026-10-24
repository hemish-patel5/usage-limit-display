
const INIT_URL =
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

browser.runtime.onMessage.addListener(async message => {
  if (message.type !== "FETCH_IMAGE_QUOTA") {
    return;
  }

  try {
    const sessionResponse = await fetch(
      "https://chatgpt.com/api/auth/session",
      { credentials: "include", cache: "no-store" }
    );

    if (!sessionResponse.ok) {
      throw new Error("Session request failed");
    }

    const session = await sessionResponse.json();

    if (!session.accessToken) {
      throw new Error("Sign in to ChatGPT first");
    }

    const headers = {
      "Content-Type": "application/json",
      "Authorization": `Bearer ${session.accessToken}`
    };

    if (session.account?.id) {
      headers["ChatGPT-Account-Id"] = session.account.id;
    }

    const response = await fetch(INIT_URL, {
      method: "POST",
      credentials: "include",
      cache: "no-store",
      headers,
      body: JSON.stringify(INIT_BODY)
    });

    if (!response.ok) {
      throw new Error(`ChatGPT HTTP ${response.status}`);
    }

    const data = await response.json();

    return {
      ok: true,
      quota: extractQuota(data)
    };
  } catch (error) {
    return {
      ok: false,
      error: error.message
    };
  }
});

/*
 * Temporary image-limit notification diagnostic.
 * Add to the bottom of content.js.
 */

(() => {
  let lastReported = "";

  function checkVisibleImageLimit() {
    if (!document.body) return;

    const text = document.body.innerText
      .replace(/[\u00a0\u202f]/g, " ")
      .replace(/[’‘]/g, "'")
      .replace(/\s+/g, " ");

    if (!/you're out of images/i.test(text)) {
      return;
    }

    const match = text.match(
      /you're out of image creations for now.{0,200}?try again tomorrow at\s+(\d{1,2}):(\d{2})\s*(AM|PM)/i
    );

    if (!match) return;

    const hour = Number(match[1]);
    const minute = Number(match[2]);

    if (
      hour < 1 || hour > 12 ||
      minute < 0 || minute > 59
    ) {
      return;
    }

    const retryTimeText =
      `${hour}:${String(minute).padStart(2, "0")} ` +
      match[3].toUpperCase();

    if (retryTimeText === lastReported) return;
    lastReported = retryTimeText;

    browser.runtime.sendMessage({
      type: "QUOTA_DIAGNOSTIC_PAGE",
      outOfImages: true,
      retryRelativeDay: "tomorrow",
      retryTimeText
    }).catch(() => {});
  }

  const observer = new MutationObserver(
    checkVisibleImageLimit
  );

  function start() {
    observer.observe(document.body, {
      childList: true,
      subtree: true,
      characterData: true
    });

    checkVisibleImageLimit();
  }

  if (document.body) {
    start();
  } else {
    document.addEventListener(
      "DOMContentLoaded",
      start,
      { once: true }
    );
  }
})();
