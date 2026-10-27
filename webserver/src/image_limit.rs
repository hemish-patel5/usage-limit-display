//! Persistent state for observed ChatGPT image-generation rejections.
//!
//! A passed reset time does not prove that image generation is available.
//! Availability remains unverified until a future reliable success signal exists.

use axum::{
    http::{HeaderMap, StatusCode},
    Json,
};
use serde::{Deserialize, Serialize};
use std::{
    io::ErrorKind,
    path::{Path, PathBuf},
    sync::OnceLock,
    time::{SystemTime, UNIX_EPOCH},
};
use tokio::sync::RwLock;

const SOURCE: &str = "chatgpt_image_rejection";
const MAX_LIMIT_SECONDS: i64 = 7 * 24 * 60 * 60;
const CLOCK_SKEW_SECONDS: i64 = 5 * 60;

#[derive(Clone, Debug, Serialize, Deserialize, PartialEq, Eq)]
struct SavedLimit {
    reset_at: i64,
    observed_at: i64,
    source: String,
}

#[derive(Debug, Serialize, PartialEq, Eq)]
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

fn state_path() -> PathBuf {
    std::env::var_os("IMAGE_LIMIT_STATE_FILE")
        .map(PathBuf::from)
        .unwrap_or_else(|| PathBuf::from("image-limit-state.json"))
}

fn now_unix() -> i64 {
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .unwrap_or_default()
        .as_secs() as i64
}

fn valid_saved_limit(saved: &SavedLimit, now: i64) -> bool {
    saved.source == SOURCE
        && saved.observed_at > 0
        && saved.observed_at <= now + CLOCK_SKEW_SECONDS
        && saved.reset_at >= saved.observed_at
        && saved.reset_at - saved.observed_at <= MAX_LIMIT_SECONDS
}

fn load_state() -> Option<SavedLimit> {
    let contents = std::fs::read_to_string(state_path()).ok()?;
    let saved = serde_json::from_str::<SavedLimit>(&contents).ok()?;

    if valid_saved_limit(&saved, now_unix()) {
        Some(saved)
    } else {
        eprintln!("Ignoring invalid persisted image-limit state");
        None
    }
}

fn state() -> &'static RwLock<Option<SavedLimit>> {
    LIMIT_STATE.get_or_init(|| RwLock::new(load_state()))
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
            status: if now < saved.reset_at {
                "limit_reached"
            } else {
                "reset_time_passed"
            },
            reset_at: Some(saved.reset_at),
            reset_in_seconds: Some(saved.reset_at.saturating_sub(now).max(0)),
            observed_at: Some(saved.observed_at),
            source: Some(saved.source.clone()),
        },
    }
}

fn token_is_valid(headers: &HeaderMap) -> Result<bool, StatusCode> {
    let expected =
        std::env::var("IMAGE_TRACKER_TOKEN").map_err(|_| StatusCode::SERVICE_UNAVAILABLE)?;
    let provided = headers
        .get("x-tracker-token")
        .and_then(|value| value.to_str().ok())
        .ok_or(StatusCode::UNAUTHORIZED)?;

    if expected.is_empty() || expected.len() != provided.len() {
        return Ok(false);
    }

    let difference = expected
        .as_bytes()
        .iter()
        .zip(provided.as_bytes())
        .fold(0_u8, |result, (left, right)| result | (left ^ right));
    Ok(difference == 0)
}

async fn persist(path: &Path, saved: &SavedLimit) -> Result<(), StatusCode> {
    let temporary = path.with_extension("json.tmp");
    let bytes = serde_json::to_vec_pretty(saved).map_err(|_| StatusCode::INTERNAL_SERVER_ERROR)?;

    tokio::fs::write(&temporary, bytes)
        .await
        .map_err(|_| StatusCode::INTERNAL_SERVER_ERROR)?;
    tokio::fs::rename(&temporary, path)
        .await
        .map_err(|_| StatusCode::INTERNAL_SERVER_ERROR)
}

// Public read route for the ESP32 over the LAN.
pub async fn read_limit() -> Json<LimitResponse> {
    let guard = state().read().await;
    Json(view(guard.as_ref(), now_unix()))
}

// Authenticated write route, mounted only on 127.0.0.1:8766.
pub async fn update_limit(
    headers: HeaderMap,
    Json(update): Json<LimitUpdate>,
) -> Result<Json<LimitResponse>, StatusCode> {
    if !token_is_valid(&headers)? {
        return Err(StatusCode::UNAUTHORIZED);
    }

    let now = now_unix();
    if update.reset_at <= now || update.reset_at > now.saturating_add(MAX_LIMIT_SECONDS) {
        return Err(StatusCode::BAD_REQUEST);
    }

    let mut guard = state().write().await;

    // Repeated detections must not silently extend an active deadline.
    if let Some(previous) = guard.as_ref() {
        if previous.reset_at > now && update.reset_at >= previous.reset_at {
            return Ok(Json(view(guard.as_ref(), now)));
        }
    }

    let saved = SavedLimit {
        reset_at: update.reset_at,
        observed_at: now,
        source: SOURCE.to_string(),
    };
    persist(&state_path(), &saved).await?;
    *guard = Some(saved);

    Ok(Json(view(guard.as_ref(), now)))
}

// Manual recovery route. It clears only image-limit state, never Codex usage.
pub async fn clear_limit(headers: HeaderMap) -> Result<StatusCode, StatusCode> {
    if !token_is_valid(&headers)? {
        return Err(StatusCode::UNAUTHORIZED);
    }

    let mut guard = state().write().await;
    match tokio::fs::remove_file(state_path()).await {
        Ok(()) => {}
        Err(error) if error.kind() == ErrorKind::NotFound => {}
        Err(_) => return Err(StatusCode::INTERNAL_SERVER_ERROR),
    }
    *guard = None;
    Ok(StatusCode::NO_CONTENT)
}

#[cfg(test)]
mod tests {
    use super::*;

    fn saved(reset_at: i64, observed_at: i64) -> SavedLimit {
        SavedLimit {
            reset_at,
            observed_at,
            source: SOURCE.to_string(),
        }
    }

    #[test]
    fn unknown_has_no_fabricated_deadline() {
        let response = view(None, 1_000);
        assert_eq!(response.status, "unknown");
        assert_eq!(response.reset_at, None);
        assert_eq!(response.reset_in_seconds, None);
    }

    #[test]
    fn active_countdown_uses_fixed_deadline() {
        let limit = saved(2_000, 1_000);
        let first = view(Some(&limit), 1_100);
        let second = view(Some(&limit), 1_110);

        assert_eq!(first.reset_at, Some(2_000));
        assert_eq!(second.reset_at, Some(2_000));
        assert_eq!(first.reset_in_seconds, Some(900));
        assert_eq!(second.reset_in_seconds, Some(890));
    }

    #[test]
    fn passed_deadline_is_not_claimed_available() {
        let response = view(Some(&saved(2_000, 1_000)), 2_001);
        assert_eq!(response.status, "reset_time_passed");
        assert_eq!(response.reset_in_seconds, Some(0));
    }

    #[test]
    fn persisted_values_are_validated() {
        assert!(valid_saved_limit(&saved(2_000, 1_000), 1_500));
        assert!(!valid_saved_limit(&saved(999, 1_000), 1_500));
        assert!(!valid_saved_limit(
            &saved(1_000 + MAX_LIMIT_SECONDS + 1, 1_000),
            1_500,
        ));
    }
}
