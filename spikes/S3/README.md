# S3 — read one power value and cross-check it against FRM

Tests claim **C5**: read the total power consumption of one circuit through the documented game API, and post it.

**Pass:** the value matches FicsitRemoteMonitoring's `getPower`, read at the same moment, within rounding.

Not started. The main repo's gate G0 did not capture the game's power API from C++ (see `mod-claims.md`), so this
claim has no source yet: a G0 addendum capturing that page comes before this spike is designed in detail.
