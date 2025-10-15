#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")"

PROJECT_DIR="$(pwd)"
SERVICE_NAME="portenta-bridge"
PROJECT_NAME="portenta_linux_bridge"
EXPECTED_TAG="${PROJECT_NAME}-${SERVICE_NAME}:latest"
COMPOSE_FILE="${PROJECT_DIR}/docker-compose.yml"

log_stage(){ echo "[bridge] STAGE:  $*"; }
log_detail(){ echo "[bridge] DETAIL: $*"; }
fail(){ echo "[bridge] ERROR:  $*"; exit 66; }  # 66 => NOINPUT-style

have_image() {
  docker image inspect "$EXPECTED_TAG" >/dev/null 2>&1
}

try_load_tar() {
  local tried=0
  # Prefer explicit tar at repo root
  if [[ -f "${PROJECT_DIR}/portenta_bridge.tar" ]]; then
    tried=1
    log_detail "Loading image from ./portenta_bridge.tar"
    LOAD_OUT="$(docker load -i "${PROJECT_DIR}/portenta_bridge.tar")"
    echo "$LOAD_OUT"
    LOADED_TAG="$(echo "$LOAD_OUT" | awk -F': ' '/Loaded image:/{print $2}' | tail -n1 || true)"
    [[ -n "${LOADED_TAG:-}" ]] && docker tag "$LOADED_TAG" "$EXPECTED_TAG"
  fi

  # Also accept any tars in image-cache/
  if ! have_image; then
    for tar in "${PROJECT_DIR}"/image-cache/*.tar; do
      [[ -e "$tar" ]] || continue
      tried=1
      log_detail "Loading image from ${tar}"
      LOAD_OUT="$(docker load -i "$tar")"
      echo "$LOAD_OUT"
      LOADED_TAG="$(echo "$LOAD_OUT" | awk -F': ' '/Loaded image:/{print $2}' | tail -n1 || true)"
      [[ -n "${LOADED_TAG:-}" ]] && docker tag "$LOADED_TAG" "$EXPECTED_TAG"
      have_image && break
    done
  fi

  return $(( have_image ? 0 : (tried==0 ? 2 : 1) ))
}

log_stage "Docker/Compose preflight"

# If the expected image isn’t present, try to load it from local tar(s)
if ! have_image; then
  log_detail "No local image found — trying to import from tar"
  if ! try_load_tar; then
    fail "Offline & no local image → cannot start. Seed once with 'docker compose build && docker compose up -d' OR drop a tar in ./ or ./image-cache/"
  fi
fi

# Start the stack strictly offline (no build)
log_stage "Starting stack (offline, no-build)"
/usr/bin/docker compose -f "$COMPOSE_FILE" up -d --no-build

log_stage "Container started — following logs"
exec /usr/bin/docker compose -f "$COMPOSE_FILE" logs -f