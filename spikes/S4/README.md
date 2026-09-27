# S4 — a credential in the mod's config, not readable by other players

Tests claim **C6**: read a token from the mod's config file.

**Pass:** the token is used; the file's location and permissions are recorded; a game client cannot read it (how
that is checked is decided from the main repo's gate-G0 sources once captured).

Not started. See `mod-claims.md` in the main repo: no source yet states the config folder or its permissions for a
dedicated server specifically.
