#!/usr/bin/env bash
# Installs or updates the game's dedicated server on first start (or with UPDATE=1), starts the loopback forward to
# the backend when BACKEND_PORT is set, then runs the server with the arguments given to the container.
set -euo pipefail

server=/srv/satis/server
if [[ ! -x "$server/FactoryServer.sh" || "${UPDATE:-0}" == "1" ]]; then
  # SteamCMD's first app_update after it updates itself can fail with "Missing configuration"; a retry succeeds
  # (seen on the first boot, 2026-11-06). Three tries, then the container exits with SteamCMD's error.
  for try in 1 2 3; do
    if /srv/satis/steamcmd/steamcmd.sh +force_install_dir "$server" +login anonymous +app_update 1690800 validate +quit; then
      break
    fi
    if [[ "$try" == 3 ]]; then exit 1; fi
    echo "app_update failed (try $try of 3); retrying"
  done
fi

# D3: the mod's URLs stay 127.0.0.1:<port> (the harness's backends are plain http on loopback, which the mod allows
# only there, CurlTransport.cpp:181; http-probe's https stubs are untrusted on purpose). Inside the container each
# port in FORWARD_PORTS (space-separated) is a forward to the same port on the Windows host, through Docker Desktop's
# host.docker.internal, so the traffic never leaves the PC.
for port in ${FORWARD_PORTS:-}; do
  [[ "$port" =~ ^[0-9]+$ ]] || { echo "FORWARD_PORTS: not a port: $port"; exit 1; }
  socat "TCP-LISTEN:${port},bind=127.0.0.1,fork,reuseaddr" "TCP:host.docker.internal:${port}" &
done

# API_RELAY=<listen>:<port>: the harness's server API calls reach the server from 127.0.0.1, as on Windows, so
# AllowInsecureLocalAccess applies (through Docker's port mapping they come from the bridge's gateway, and the server
# refuses Shutdown with insufficient_scope; seen 2026-11-06). The host publishes its loopback TCP <port> to <listen>.
if [[ -n "${API_RELAY:-}" ]]; then
  [[ "$API_RELAY" =~ ^([0-9]+):([0-9]+)$ ]] || { echo "API_RELAY: expected <listen>:<port>, got $API_RELAY"; exit 1; }
  socat "TCP-LISTEN:${BASH_REMATCH[1]},fork,reuseaddr" "TCP:127.0.0.1:${BASH_REMATCH[2]}" &
fi

exec "$server/FactoryServer.sh" -log -unattended "$@"
