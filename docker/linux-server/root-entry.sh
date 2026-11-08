#!/usr/bin/env bash
# Root entry point. A Linux container has no DNS client cache, which S12 reads on Windows to name the server's outbound
# addresses. So: start dnsmasq as the container's local resolver (127.0.0.1 only, forwarding to the upstreams Docker wrote,
# logging every query to /var/log/satis-dns.log), point /etc/resolv.conf at it, then drop to the steam user and run
# entrypoint.sh with the container's environment and arguments. The harness parses the log (harness/src/outbound-docker.ts).
set -euo pipefail

upstreams=$(awk '$1 == "nameserver" { print $2 }' /etc/resolv.conf)
if [[ -z "$upstreams" ]]; then
  echo "root-entry: no nameserver in /etc/resolv.conf; cannot start the DNS cache" >&2
  exit 1
fi

# --user/--group steam: dnsmasq binds port 53 as root, then runs as steam, so its log is steam's and the harness reads it
# with `docker exec -u steam` (root can't read the server's /proc/<pid>/fd without CAP_SYS_PTRACE).
args=(--no-resolv --listen-address=127.0.0.1 --bind-interfaces --log-queries --log-facility=/var/log/satis-dns.log --user=steam --group=steam)
for ns in $upstreams; do args+=("--server=$ns"); done
dnsmasq "${args[@]}"
# dnsmasq forks into the background (it exits non-zero first if it can't bind); check it stayed up.
sleep 1
if ! pgrep -x dnsmasq > /dev/null; then
  echo "root-entry: dnsmasq is not running" >&2
  exit 1
fi

# Keep any search/options lines Docker wrote; only the nameservers change.
{ echo "nameserver 127.0.0.1"; grep -E '^(search|options)[[:space:]]' /etc/resolv.conf || true; } > /tmp/resolv.new
cat /tmp/resolv.new > /etc/resolv.conf
rm -f /tmp/resolv.new

exec setpriv --reuid=1000 --regid=1000 --init-groups env HOME=/srv/satis /srv/satis/entrypoint.sh "$@"
