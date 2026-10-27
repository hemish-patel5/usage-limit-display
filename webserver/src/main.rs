mod image_limit;

use axum::{
    extract::{DefaultBodyLimit, State},
    http::{header, Method},
    routing::get,
    Json, Router,
};
use serde::Serialize;
use serde_json::{json, Value};
use std::{
    error::Error,
    process::Stdio,
    sync::Arc,
    time::{Duration, SystemTime, UNIX_EPOCH},
};
use tokio::{
    io::{AsyncBufReadExt, AsyncWriteExt, BufReader},
    net::TcpListener,
    process::{Child, ChildStdin, ChildStdout, Command},
    sync::RwLock,
    time::{sleep, timeout},
};
use tower_http::cors::{Any, CorsLayer};

type AppResult<T> = Result<T, Box<dyn Error + Send + Sync>>;

type SharedState = Arc<RwLock<UsageResponse>>;

const REFRESH_INTERVAL: u64 = 300;
const SERVER_ADDRESS: &str = "0.0.0.0:8765";

#[derive(Clone, Serialize)]
struct UsageWindow {
    used_percent: f64,
    resets_at: Option<i64>,
    window_duration_mins: u64,
}

#[derive(Clone, Serialize)]
struct UsageResponse {
    status: String,
    five_hour: Option<UsageWindow>,
    weekly: Option<UsageWindow>,
    updated_at: Option<u64>,
    error: Option<String>,
}

impl Default for UsageResponse {
    fn default() -> Self {
        Self {
            status: "starting".into(),
            five_hour: None,
            weekly: None,
            updated_at: None,
            error: None,
        }
    }
}

struct CodexClient {
    _child: Child,
    stdin: ChildStdin,
    stdout: BufReader<ChildStdout>,
    request_id: u64,
}

impl CodexClient {
    async fn connect() -> AppResult<Self> {
        let mut child = Command::new("codex")
            .arg("app-server")
            .stdin(Stdio::piped())
            .stdout(Stdio::piped())
            .stderr(Stdio::inherit())
            .kill_on_drop(true)
            .spawn()?;

        let stdin = child.stdin.take().ok_or("Missing stdin")?;

        let stdout = child.stdout.take().ok_or("Missing stdout")?;

        let mut client = Self {
            _child: child,
            stdin,
            stdout: BufReader::new(stdout),
            request_id: 0,
        };

        // Initialize the Codex JSON-RPC connection.
        client
            .request(
                "initialize",
                Some(json!({
                    "clientInfo": {
                        "name": "usage_limit_tracker",
                        "title": "Usage Limit Tracker",
                        "version": "0.1.0"
                    }
                })),
            )
            .await?;

        client
            .write_message(&json!({
                "method": "initialized",
                "params": {}
            }))
            .await?;

        Ok(client)
    }

    async fn write_message(&mut self, message: &Value) -> AppResult<()> {
        let mut bytes = serde_json::to_vec(message)?;
        bytes.push(b'\n');

        self.stdin.write_all(&bytes).await?;
        self.stdin.flush().await?;

        Ok(())
    }

    async fn request(&mut self, method: &str, params: Option<Value>) -> AppResult<Value> {
        self.request_id += 1;
        let id = self.request_id;

        let mut message = json!({
            "id": id,
            "method": method
        });

        if let Some(params) = params {
            message["params"] = params;
        }

        self.write_message(&message).await?;

        loop {
            let mut line = String::new();

            let count = self.stdout.read_line(&mut line).await?;

            if count == 0 {
                return Err("Codex App Server disconnected".into());
            }

            let response: Value = serde_json::from_str(&line)?;

            // Ignore unrelated notifications.
            if response.get("id") != Some(&json!(id)) {
                continue;
            }

            if let Some(error) = response.get("error") {
                return Err(format!("Codex error: {error}").into());
            }

            return Ok(response.get("result").cloned().unwrap_or(Value::Null));
        }
    }

    async fn get_usage(&mut self) -> AppResult<UsageResponse> {
        let result = self.request("account/rateLimits/read", None).await?;

        // Prefer the named Codex bucket when present.
        let limits = result
            .pointer("/rateLimitsByLimitId/codex")
            .or_else(|| result.get("rateLimits"))
            .ok_or("No rate limits returned")?;

        let windows = [limits.get("primary"), limits.get("secondary")];

        let five_hour = find_window(&windows, 300);
        let weekly = find_window(&windows, 10080);

        let updated_at = SystemTime::now().duration_since(UNIX_EPOCH)?.as_secs();

        Ok(UsageResponse {
            status: "ok".into(),
            five_hour,
            weekly,
            updated_at: Some(updated_at),
            error: None,
        })
    }
}

fn find_window(windows: &[Option<&Value>], duration: u64) -> Option<UsageWindow> {
    windows.iter().flatten().find_map(|window| {
        let minutes = window.get("windowDurationMins")?.as_u64()?;

        if minutes != duration {
            return None;
        }

        Some(UsageWindow {
            used_percent: window.get("usedPercent")?.as_f64()?,
            resets_at: window.get("resetsAt").and_then(Value::as_i64),
            window_duration_mins: minutes,
        })
    })
}

async fn refresh_worker(state: SharedState) {
    loop {
        println!("Connecting to Codex...");

        let connection = timeout(Duration::from_secs(20), CodexClient::connect()).await;

        match connection {
            Ok(Ok(mut client)) => {
                println!("Codex connected!");

                loop {
                    let result = timeout(Duration::from_secs(30), client.get_usage()).await;

                    match result {
                        Ok(Ok(usage)) => {
                            println!(
                                "Usage refreshed: 5h={:?}, weekly={:?}",
                                usage.five_hour.as_ref().map(|w| w.used_percent),
                                usage.weekly.as_ref().map(|w| w.used_percent)
                            );

                            *state.write().await = usage;
                        }
                        other => {
                            let message = match other {
                                Ok(Err(error)) => error.to_string(),
                                Err(_) => "Request timed out".into(),
                                _ => unreachable!(),
                            };

                            eprintln!("Refresh failed: {message}");

                            let mut current = state.write().await;
                            current.status = "error".into();
                            current.error = Some(message);

                            break;
                        }
                    }

                    sleep(Duration::from_secs(REFRESH_INTERVAL)).await;
                }
            }
            other => {
                let message = match other {
                    Ok(Err(error)) => error.to_string(),
                    Err(_) => "Connection timed out".into(),
                    _ => unreachable!(),
                };

                eprintln!("Connection failed: {message}");

                let mut current = state.write().await;
                current.status = "error".into();
                current.error = Some(message);
            }
        }

        // Reconnect after an error.
        sleep(Duration::from_secs(15)).await;
    }
}

async fn health_handler() -> Json<Value> {
    Json(json!({
        "service": "usage-limit-tracker",
        "status": "running"
    }))
}

async fn usage_handler(State(state): State<SharedState>) -> Json<UsageResponse> {
    Json(state.read().await.clone())
}

#[tokio::main]
async fn main() -> AppResult<()> {
    let state = Arc::new(RwLock::new(UsageResponse::default()));

    tokio::spawn(refresh_worker(state.clone()));

    let app = Router::new()
        .route("/health", get(health_handler))
        .route("/usage", get(usage_handler))
        .route("/image-limit", get(image_limit::read_limit))
        .with_state(state);

    // Separate server for incoming Firefox updates.
    // Only accessible from this laptop.

    let update_app = Router::new()
        .route(
            "/images/limit-update",
            axum::routing::post(image_limit::update_limit).delete(image_limit::clear_limit),
        )
        // This listener is loopback-only and every state-changing request is
        // authenticated. CORS is needed because Firefox extension pages have
        // a generated moz-extension:// origin and send a preflight request.
        .layer(
            CorsLayer::new()
                .allow_origin(Any)
                .allow_methods([Method::POST, Method::DELETE])
                .allow_headers([
                    header::CONTENT_TYPE,
                    header::HeaderName::from_static("x-tracker-token"),
                ]),
        )
        .layer(DefaultBodyLimit::max(1024));

    let update_listener = TcpListener::bind("127.0.0.1:8766").await?;

    tokio::spawn(async move {
        if let Err(error) = axum::serve(update_listener, update_app).await {
            eprintln!("Image update server failed: {error}");
        }
    });

    let listener = TcpListener::bind(SERVER_ADDRESS).await?;

    println!("================================");
    println!("  Codex Usage Limit Tracker");
    println!("================================");
    println!("Server: http://{SERVER_ADDRESS}");
    println!("Health: /health");
    println!("Usage:  /usage");
    println!("Refresh: every 5 minutes");

    axum::serve(listener, app).await?;

    Ok(())
}
