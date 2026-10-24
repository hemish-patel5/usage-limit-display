
/*
 * ChatGPT Image Quota Diagnostic v4
 *
 * Temporary diagnostic only.
 * Does not log conversation text, credentials,
 * cookies, tokens or full response bodies.
 */

(() => {
  const MAX_SCAN_BYTES = 2 * 1024 * 1024;
  const TAIL_LENGTH = 2048;

  const requests = new Map();

  const filterOptions = {
    urls: ["https://chatgpt.com/backend-api/*"],
    types: ["xmlhttprequest"]
  };

  const signalPattern =
    /image_gen|image.generation|image generations|out of images|out of image creations|rate.limit|retry.after|reset.after|reset.at|limit resets in|try again tomorrow/gi;

  const safeFieldPattern =
    /"(retry_after(?:_ms)?|reset_after|reset_at|resets_at|remaining)"\s*:\s*(?:"([0-9TZ:.+\-]+)"|(\d+(?:\.\d+)?))/gi;

  const headerNames = new Set([
    "retry-after",
    "ratelimit-reset",
    "ratelimit-remaining",
    "x-ratelimit-reset",
    "x-ratelimit-remaining"
  ]);

  function sanitizePath(url) {
    try {
      return new URL(url).pathname
        .replace(
          /\/conversations\/[^/]+/,
          "/conversations/:id"
        );
    } catch {
      return "unknown";
    }
  }

  function normalizeText(text) {
    return text
      .replace(/\\n|\\r|\\t/g, " ")
      .replace(/\\u00a0|\\u202f/gi, " ")
      .replace(/[\u00a0\u202f]/g, " ")
      .replace(/[’‘]/g, "'")
      .replace(/\s+/g, " ");
  }

  function detectMessages(text, state) {
    const normalized = normalizeText(text);

    // Message 1:
    // You've hit the Plus plan limit ...
    // ... limit resets in 19 hours.
    const limitPattern =
      /you've hit the plus plan limit for image generations requests/i;

    if (limitPattern.test(normalized)) {
      state.limitMessage = true;
    }

    const hoursMatch = normalized.match(
      /you've hit the plus plan limit for image generations requests.{0,500}?limit resets in\s+(\d+)\s+hours?/i
    );

    if (hoursMatch) {
      state.limitMessage = true;
      state.limitHours = Number(hoursMatch[1]);
    }

    // Message 2:
    // You're out of images.
    // You're out of image creations for now.
    // Try again tomorrow at 6:27 PM.
    const outOfImagesPattern =
      /you're out of images|you're out of image creations for now/i;

    if (outOfImagesPattern.test(normalized)) {
      state.outOfImages = true;
    }

    const tomorrowMatch = normalized.match(
      /try again tomorrow at\s+(\d{1,2}):(\d{2})\s*(AM|PM)/i
    );

    if (tomorrowMatch) {
      const hour = Number(tomorrowMatch[1]);
      const minute = Number(tomorrowMatch[2]);

      if (
        hour >= 1 &&
        hour <= 12 &&
        minute >= 0 &&
        minute <= 59
      ) {
        state.outOfImages = true;

        state.retryTimeText =
          `${hour}:${String(minute).padStart(2, "0")} ` +
          tomorrowMatch[3].toUpperCase();

        state.retryRelativeDay = "tomorrow";
      }
    }
  }

  function report(state) {
    const interesting =
      state.signals.size > 0 ||
      state.limitMessage ||
      state.outOfImages ||
      state.status >= 400 ||
      Object.keys(state.rateHeaders).length > 0;

    if (!interesting) return;

    console.info("[quota diagnostic v4]", {
      source: "network",
      path: state.path,
      method: state.method,
      status: state.status,
      signals: [...state.signals],
      fields: Object.fromEntries(state.fields),
      limitMessage: state.limitMessage,
      limitHours: state.limitHours,
      outOfImages: state.outOfImages,
      retryRelativeDay: state.retryRelativeDay,
      retryTimeText: state.retryTimeText,
      rateHeaders: state.rateHeaders,
      scannedBytes: state.scannedBytes
    });
  }

  browser.webRequest.onBeforeRequest.addListener(
    details => {
      if (!["GET", "POST"].includes(details.method)) {
        return;
      }

      if (details.tabId < 0) return;

      const state = {
        path: sanitizePath(details.url),
        method: details.method,
        status: 0,
        signals: new Set(),
        fields: new Map(),
        rateHeaders: {},
        scannedBytes: 0,
        tail: "",
        limitMessage: false,
        limitHours: null,
        outOfImages: false,
        retryRelativeDay: null,
        retryTimeText: null
      };

      requests.set(details.requestId, state);

      let filter;

      try {
        filter = browser.webRequest.filterResponseData(
          details.requestId
        );
      } catch (error) {
        requests.delete(details.requestId);
        console.warn(
          "[quota diagnostic v4] filter unavailable",
          state.path,
          error.message
        );
        return;
      }

      const decoder = new TextDecoder();

      function inspect(text) {
        const combined = state.tail + text;

        for (const match of combined.matchAll(signalPattern)) {
          state.signals.add(match[0].toLowerCase());
        }

        for (const match of combined.matchAll(safeFieldPattern)) {
          state.fields.set(
            match[1],
            match[2] ?? match[3]
          );
        }

        detectMessages(combined, state);

        state.tail = combined.slice(-TAIL_LENGTH);
      }

      filter.ondata = event => {
        // Always forward the original bytes unchanged.
        filter.write(event.data);

        const bytes = new Uint8Array(event.data);
        const allowance =
          MAX_SCAN_BYTES - state.scannedBytes;

        if (allowance <= 0) return;

        const portion = bytes.subarray(
          0,
          Math.min(bytes.length, allowance)
        );

        state.scannedBytes += portion.length;

        try {
          inspect(
            decoder.decode(portion, { stream: true })
          );
        } catch (error) {
          console.warn(
            "[quota diagnostic v4] inspection error",
            error.message
          );
        }
      };

      filter.onstop = () => {
        try {
          if (state.scannedBytes < MAX_SCAN_BYTES) {
            inspect(decoder.decode());
          }
        } catch (_) {}

        try {
          filter.disconnect();
        } catch (_) {}

        report(state);
        requests.delete(details.requestId);
      };

      filter.onerror = () => {
        try {
          filter.disconnect();
        } catch (_) {}

        requests.delete(details.requestId);
      };
    },
    filterOptions,
    ["blocking"]
  );

  browser.webRequest.onHeadersReceived.addListener(
    details => {
      const state = requests.get(details.requestId);
      if (!state) return;

      state.status = details.statusCode;

      for (const header of details.responseHeaders || []) {
        const name = header.name.toLowerCase();

        if (
          headerNames.has(name) &&
          typeof header.value === "string" &&
          /^[\w\s,.:;+\-]{1,90}$/.test(header.value)
        ) {
          state.rateHeaders[name] = header.value;
        }
      }
    },
    filterOptions,
    ["responseHeaders"]
  );

  browser.webRequest.onErrorOccurred.addListener(
    details => {
      const state = requests.get(details.requestId);
      if (!state) return;

      console.warn("[quota diagnostic v4] request failed", {
        path: state.path,
        method: state.method,
        error: details.error
      });

      requests.delete(details.requestId);
    },
    filterOptions
  );

  // Receives optional detection reports from content.js.
  browser.runtime.onMessage.addListener(message => {
    if (message?.type !== "QUOTA_DIAGNOSTIC_PAGE") {
      return;
    }

    console.info("[quota diagnostic v4]", {
      source: "visible_page",
      outOfImages: message.outOfImages === true,
      retryRelativeDay: message.retryRelativeDay ?? null,
      retryTimeText: message.retryTimeText ?? null,
      observedAt: new Date().toISOString()
    });
  });

  console.info(
    "[quota diagnostic v4] monitoring image quotas " +
    "and image reset notifications"
  );
})();
