# The Linux test server

The game's Linux dedicated server in a container, for the mod's Linux runs (plan
`docs/plans/2026-11-06-linux-server-build.md`, L2). The image is our own: Debian bookworm-slim, Valve's SteamCMD and the
server (Steam app 1690800). No third-party server image, so we know everything in it.

## Layout

- `/srv/satis/server`: the game install, in the named volume `satis-linux-server` (inside Docker Desktop's disk
  image on D:). It installs on first start, or again with `-e UPDATE=1`.
- `/srv/satis/server/FactoryGame/Saved`: bind-mounted from `D:\satis-linux\Saved` (logs, the mod's config and
  credential), so the harness reads them with plain Windows paths.
- `/srv/satis/server/FactoryGame/Mods`: bind-mounted from `D:\satis-linux\Mods` (SML and the mod's LinuxServer build).
- Runs as the non-root user `steam` (uid 1000), with its home at `/srv/satis`.

## Ports

The game ports are published to loopback only: 27777 (TCP and UDP) and 28888 (TCP). The Windows test server keeps
17777 and 18888.

The mod in the container reaches the harness's loopback services through `FORWARD_PORTS` (space-separated): each port
becomes a forward from the container's 127.0.0.1 to the same port on the Windows host (`host.docker.internal`). The
mod's URLs stay `http://127.0.0.1:<port>`, and nothing leaves the PC.

## Trust

There's no certificate change. The harness's backends are plain http on loopback, which the mod allows only there
(`mod/.../CurlTransport.cpp:181`), and http-probe's https stubs are untrusted on purpose (S1). On Linux the mod trusts
what a real Linux host's server trusts: the engine's `cacert.pem` plus the system bundle
(`Engine/Source/Runtime/Online/SSL/Private/Unix/UnixPlatformSslCertificateManager.cpp:23-41`).

## By hand

```
docker build -t satis-linux-ds:local docker/linux-server
docker run -d --name satis-linux-ds -v satis-linux-server:/srv/satis/server -v D:\satis-linux\Saved:/srv/satis/server/FactoryGame/Saved -v D:\satis-linux\Mods:/srv/satis/server/FactoryGame/Mods -p 127.0.0.1:27777:27777/tcp -p 127.0.0.1:27777:27777/udp -p 127.0.0.1:28888:28888/tcp satis-linux-ds:local -Port=27777 -ReliablePort=28888
```

Stop it with the server API's `Shutdown` first, then `docker stop satis-linux-ds` and `docker rm satis-linux-ds`.
