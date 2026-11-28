// OBS adapter: the League -> Match pickers shared by the scoreboard and the panel
// sources, and the localized texts they show. UI thread.
#pragma once

#include "core/match.hpp"
#include "core/shared_selection.hpp"

#include <obs-module.h>

#include <QLocale>
#include <QString>

#include <cstdint>
#include <string>

namespace fscore::obs {

// Settings keys both sources use.
inline constexpr const char *kLeagueKey = "league";
inline constexpr const char *kMatchKey = "match";
// On: this source shares its league and match with every other linked source.
inline constexpr const char *kLinkedKey = "linked";

// The league and match every linked source shows (the replay dock follows it too).
SharedSelection &shared_selection();

// Start of a source's update: applies the link and returns the league and match to
// show (already written back to `settings` when the group's differ). The caller
// guards `state`; `previous` is what the source was showing.
Selection sync_on_update(obs_data_t *settings, LinkState &state, const Selection &previous, bool has_match);
// Every tick: when another linked source changed the selection, pushes it into this
// source's settings (its update callback follows on a later tick).
void sync_on_tick(obs_source_t *source, LinkState &state, const Selection &current, bool has_match);

QLocale obs_locale();

// "TODAY", "TOMORROW" or "SAT 22 NOV", in local time.
QString day_label(int64_t start_utc, const QLocale &locale);
QString kickoff_time(int64_t start_utc, const QLocale &locale);
QString score_text(const Match &match);

// "Real Madrid vs Barcelona · LIVE 67' 2 - 1", "... · Sat 22 Nov, 20:00", "... · FT 2 - 1"
std::string match_label(const Match &match, const QLocale &locale);

// Refills the Match list for `league`, keeping `selected` choosable.
void fill_match_list(obs_property_t *list, const std::string &league, const std::string &selected, bool force);

// Adds the "linked" checkbox and the League and Match lists (the Match list follows the League one).
void add_league_and_match(obs_properties_t *props, const std::string &league, const std::string &selected);

} // namespace fscore::obs
