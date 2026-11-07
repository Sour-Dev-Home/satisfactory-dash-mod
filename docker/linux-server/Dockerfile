# The Linux dedicated server for the mod's Linux test runs (plan docs/plans/2026-11-06-linux-server-build.md, L2, D1).
# Our own image: Debian, Valve's SteamCMD and the game's server (app 1690800), nothing else. No third-party server
# image, so we know everything in it (clean room, ADR-0058 rule 7).
FROM debian:bookworm-slim

# lib32gcc-s1 and lib32stdc++6: SteamCMD is a 32-bit program. socat: the loopback forward to the backend (D3).
# procps: ps and /proc tools for the harness's memory reads (Task 6).
RUN apt-get update \
 && apt-get install -y --no-install-recommends ca-certificates curl lib32gcc-s1 lib32stdc++6 procps socat \
 && rm -rf /var/lib/apt/lists/*

# A non-root user owns everything under /srv/satis (its home too, where SteamCMD keeps its own files).
RUN useradd --uid 1000 --home-dir /srv/satis --create-home --shell /bin/bash steam
USER steam
WORKDIR /srv/satis

# FactoryGame/{Saved,Mods,Configs} exist (owned by steam) before the volume is made: otherwise Docker creates them as
# root for the bind mounts, and SteamCMD can't write FactoryGame/Binaries (state 0x602, first boot 2026-11-06).
RUN mkdir -p steamcmd server/FactoryGame/Saved server/FactoryGame/Mods server/FactoryGame/Configs \
 && curl -fsSL https://steamcdn-a.akamaihd.net/client/installer/steamcmd_linux.tar.gz | tar -xz -C steamcmd

COPY --chown=steam:steam entrypoint.sh /srv/satis/entrypoint.sh
RUN chmod +x /srv/satis/entrypoint.sh

# The game install (a named volume, D2); FactoryGame/Saved and FactoryGame/Mods inside it are bind mounts from D:.
VOLUME ["/srv/satis/server"]
ENTRYPOINT ["/srv/satis/entrypoint.sh"]
