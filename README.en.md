# OBS Football Scores

Native OBS Studio plugin that shows **one football match** with its live score, team crests and the league's flag.

In the source properties pick **League → Match** (e.g. *LaLiga → Real Madrid vs Barcelona*) and the source shows only that match.

[Español](README.md)

## Features

- **League → Match**: 30 competitions (LaLiga, Premier League, Serie A, Bundesliga, Ligue 1, Champions League, Libertadores, Liga BetPlay, Liga MX, MLS, World Cup, Nations League…).
- **Live now**: the "🔴 Live now (all leagues)" option lists only the matches being played right now in any competition ESPN covers, with their league.
- **Clean list**: the match list only shows live and upcoming matches; finished ones drop out.
- **Fixed or automatic match**: choose one match, or "Automatic" for the league's live match (otherwise the last result for a few hours, then the next match).
- **Live score**: minute, half time and full time; before kick-off, the local time and day. Refreshes every 15 s during the match.
- **Crests and flags**: each team's crest (the flag for national teams), the league logo and its country flag.
- **Goal animation** with "GOAL!", the scorer and their photo when ESPN has one (otherwise the crest); a button and a hotkey to test it.
- **6 designs**: Classic (slim bar), Stadium (crests pop out of the bar), Clash (diagonal team colors), Card (for a corner), Floating (big crests over the score, transparent background) and Clean (crests and score only). The large-crest designs keep their proportions: you only pick the width.
- **Styles** for Classic: Minimal, Bold and Neon; accent and text colors, full/short/abbreviated names, any size.

## Install

Download your platform's file from [Releases](https://github.com/alejox/obs-football-scores/releases) and follow `INSTALL.txt`. Then in OBS: **Sources → + → Football Scoreboard**.

## Data

Scores, crests and flags come from ESPN's public API (`site.api.espn.com`, `a.espncdn.com`); no key or account needed. The API is unofficial and may change without notice.

## Build

```bash
cmake --preset linux-x86_64        # or windows-x64 / macos
cmake --build --preset linux-x86_64
cmake -S tests -B build_tests && cmake --build build_tests && ctest --test-dir build_tests
```

License: GPL-2.0, like OBS Studio.
