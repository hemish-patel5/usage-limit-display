//! Lightweight Linux CPU and memory monitoring for the ESP32 dashboard.

use axum::Json;
use serde::Serialize;
use std::{
    sync::{Arc, OnceLock},
    time::{Duration, SystemTime, UNIX_EPOCH},
};
use tokio::{sync::RwLock, time::sleep};

const SAMPLE_INTERVAL: Duration = Duration::from_secs(2);

#[derive(Clone, Debug, Default, Serialize)]
pub struct PerformanceResponse {
    status: &'static str,
    cpu_percent: Option<f64>,
    ram_percent: Option<f64>,
    updated_at: Option<u64>,
}

#[derive(Clone, Copy, Debug)]
struct CpuTimes {
    total: u64,
    idle: u64,
}

static PERFORMANCE_STATE: OnceLock<Arc<RwLock<PerformanceResponse>>> = OnceLock::new();

fn state() -> &'static Arc<RwLock<PerformanceResponse>> {
    PERFORMANCE_STATE.get_or_init(|| {
        Arc::new(RwLock::new(PerformanceResponse {
            status: "starting",
            ..PerformanceResponse::default()
        }))
    })
}

fn parse_cpu_times(contents: &str) -> Option<CpuTimes> {
    let line = contents.lines().next()?;
    let mut fields = line.split_whitespace();
    if fields.next()? != "cpu" {
        return None;
    }

    // Linux accounts guest time inside user/nice, so only the first eight
    // counters are included to avoid counting guest work twice.
    let values: Vec<u64> = fields
        .take(8)
        .map(str::parse)
        .collect::<Result<_, _>>()
        .ok()?;
    if values.len() < 4 {
        return None;
    }

    let total = values.iter().sum();
    let idle = values[3] + values.get(4).copied().unwrap_or(0);
    Some(CpuTimes { total, idle })
}

fn cpu_percent(previous: CpuTimes, current: CpuTimes) -> Option<f64> {
    let total = current.total.checked_sub(previous.total)?;
    let idle = current.idle.checked_sub(previous.idle)?;
    if total == 0 || idle > total {
        return None;
    }

    Some(((total - idle) as f64 * 100.0 / total as f64).clamp(0.0, 100.0))
}

fn parse_ram_percent(contents: &str) -> Option<f64> {
    let mut total = None;
    let mut available = None;

    for line in contents.lines() {
        let mut fields = line.split_whitespace();
        match fields.next()? {
            "MemTotal:" => total = fields.next()?.parse::<u64>().ok(),
            "MemAvailable:" => available = fields.next()?.parse::<u64>().ok(),
            _ => {}
        }
    }

    let total = total?;
    let available = available?;
    if total == 0 || available > total {
        return None;
    }

    Some(((total - available) as f64 * 100.0 / total as f64).clamp(0.0, 100.0))
}

async fn read_cpu_times() -> Option<CpuTimes> {
    parse_cpu_times(&tokio::fs::read_to_string("/proc/stat").await.ok()?)
}

async fn read_ram_percent() -> Option<f64> {
    parse_ram_percent(&tokio::fs::read_to_string("/proc/meminfo").await.ok()?)
}

pub async fn refresh_worker() {
    let mut previous = read_cpu_times().await;

    loop {
        sleep(SAMPLE_INTERVAL).await;

        let current = read_cpu_times().await;
        let cpu = previous
            .zip(current)
            .and_then(|(old, new)| cpu_percent(old, new));
        let ram = read_ram_percent().await;
        previous = current;

        let mut response = state().write().await;
        match (cpu, ram) {
            (Some(cpu_percent), Some(ram_percent)) => {
                *response = PerformanceResponse {
                    status: "ok",
                    cpu_percent: Some(cpu_percent),
                    ram_percent: Some(ram_percent),
                    updated_at: SystemTime::now()
                        .duration_since(UNIX_EPOCH)
                        .ok()
                        .map(|duration| duration.as_secs()),
                };
            }
            _ => {
                response.status = "error";
            }
        }
    }
}

pub async fn read_performance() -> Json<PerformanceResponse> {
    Json(state().read().await.clone())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn calculates_cpu_usage_from_counter_deltas() {
        let previous = CpuTimes {
            total: 1_000,
            idle: 600,
        };
        let current = CpuTimes {
            total: 1_100,
            idle: 640,
        };
        assert_eq!(cpu_percent(previous, current), Some(60.0));
    }

    #[test]
    fn parses_available_memory_percentage() {
        let contents = "MemTotal:       1000 kB\nMemAvailable:    320 kB\n";
        assert_eq!(parse_ram_percent(contents), Some(68.0));
    }
}
