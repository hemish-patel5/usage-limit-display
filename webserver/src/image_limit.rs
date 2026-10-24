//! Server-side state for the *observed* ChatGPT image-generation restriction.
//! This intentionally does not claim that conversation/init's `remaining` is usable.
use axum::{
    http::{HeaderMap, StatusCode},
    Json,
};
use serde::{Deserialize, Serialize};
use std::{path::PathBuf, sync::OnceLock, time::{SystemTime, UNIX_EPOCH}};
use tokio::sync::RwLock;

#[derive(Clone, Serialize, Deserialize)]
struct SavedLimit {
    reset_at: i64,
    observed_at: i64,
    source: String,
}

#[derive(Serialize)]
pub struct LimitResponse {
    status: &'static str,
    reset_at: Option<i64>,
    reset_in_seconds: Option<i64>,
    observed_at: Option<i64>,
    source: Option<String>,
}

#[derive(Deserialize)]
pub struct LimitUpdate {
    reset_at: i64,
}

static LIMIT_STATE: OnceLock<RwLock<Option<SavedLimit>>> = OnceLock::new();

fn path() -> PathBuf {
    std::env::var_os("IMAGE_LIMIT_STATE_FILE")
        .map(PathBuf::from)
        .unwrap_or_else(|| PathBuf::from("image-limit-state.json"))
}

fn state() -> &'static RwLock<Option<SavedLimit>> {
    LIMIT_STATE.get_or_init(|| {
        let saved = std::fs::read_to_string(path()).ok()
            .and_then(|s| serde_json::from_str::<SavedLimit>(&s).ok());
        RwLock::new(saved)
    })
}

fn now_unix() -> i64 {
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .unwrap_or_default()
        .as_secs() as i64
}

fn view(saved: Option<&SavedLimit>, now: i64) -> LimitResponse {
    match saved {
        None => LimitResponse {
            status: "unknown",
            reset_at: None,
            reset_in_seconds: None,
            observed_at: None,
            source: None,
        },
        Some(saved) => LimitResponse {
            // Passing the displayed time does not prove image access is restored.
            status: if now < saved.reset_at {
                "limit_reached"
            } else {
                "reset_time_passed"
            },
            reset_at: Some(saved.reset_at),
            reset_in_seconds: Some((saved.reset_at - now).max(0)),
            observed_at: Some(saved.observed_at),
            source: Some(saved.source.clone()),
        },
    }
}

// Public read route, intended for the ESP32 over the LAN.
pub async fn read_limit() -> Json<LimitResponse> {
    let guard = state().read().await;
    Json(view(guard.as_ref(), now_unix()))
}

// Write route, hosted only on 127.0.0.1:8766, with x-tracker-token.
pub async fn update_limit(
    headers: HeaderMap,
    Json(update): Json<LimitUpdate>,
) -> Result<Json<LimitResponse>, StatusCode> {
    let expected = std::env::var("IMAGE_TRACKER_TOKEN")
        .map_err(|_| StatusCode::SERVICE_UNAVAILABLE)?;
    let provided = headers
        .get("x-tracker-token")
        .and_then(|v| v.to_str().ok())
        .ok_or(StatusCode::UNAUTHORIZED)?;

    if expected.is_empty() || provided != expected {
        return Err(StatusCode::UNAUTHORIZED);
    }

    let now = now_unix();
    // Only accept plausible future deadlines, never arbitrary timestamps.
    if update.reset_at <= now || update.reset_at > now + 48 * 3600 {
        return Err(StatusCode::BAD_REQUEST);
    }

    let mut guard = state().write().await;

    // If already blocked, never move its deadline *later* just because
    // the same "tomorrow at ..." wording is encountered again.
    if let Some(previous) = guard.as_ref() {
        if previous.reset_at > now && update.reset_at >= previous.reset_at {
            return Ok(Json(view(guard.as_ref(), now)));
        }
    }

    let new_limit = SavedLimit {
        reset_at: update.reset_at,
        observed_at: now,
        source: "chatgpt_image_rejection".to_string(),
    };

    let bytes = serde_json::to_vec_pretty(&new_limit)
        .map_err(|_| StatusCode::INTERNAL_SERVER_ERROR)?;
    tokio::fs::write(path(), bytes).await
        .map_err(|_| StatusCode::INTERNAL_SERVER_ERROR)?;

    *guard = Some(new_limit);
    Ok(Json(view(guard.as_ref(), now)))
}
