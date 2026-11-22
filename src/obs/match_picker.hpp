// OBS adapter: the League -> Match pickers shared by the scoreboard and the panel
// sources, and the localized texts they show. UI thread.
#pragma once

#include "core/match.hpp"

#include <obs-module.h>

#include <QLocale>
#include <QString>

#include <cstdint>
#include <string>

namespace fscore::obs {

// Settings keys both sources use.
inline constexpr const char *kLeagueKey = "league";
inline constexpr const char *kMatchKey = "match";

QLocale obs_locale();

// "TODAY", "TOMORROW" or "SAT 22 NOV", in local time.
QString day_label(int64_t start_utc, const QLocale &locale);
QString kickoff_time(int64_t start_utc, const QLocale &locale);
QString score_text(const Match &match);

// "Real Madrid vs Barcelona · LIVE 67' 2 - 1", "... · Sat 22 Nov, 20:00", "... · FT 2 - 1"
std::string match_label(const Match &match, const QLocale &locale);

// Refills the Match list for `league`, keeping `selected` choosable.
void fill_match_list(obs_property_t *list, const std::string &league, const std::string &selected, bool force);

// Adds the League and Match lists (the Match list follows the League one).
void add_league_and_match(obs_properties_t *props, const std::string &league, const std::string &selected);

} // namespace fscore::obs
