// Pure core: no OBS, no Qt.
#pragma once

#include <string>
#include <vector>

namespace fscore {

struct League {
	const char *slug;    // ESPN league id: "esp.1"
	const char *name;    // "LaLiga"
	const char *country; // ESPN country code for the flag ("esp"); empty for international competitions
	const char *espn_id; // ESPN's numeric league id ("740"), as in event uids "s:600~l:740~e:..."
	const char *logo;    // league logo for dark backgrounds
};

// Pseudo league: every match ESPN lists today, whatever the league.
inline constexpr const char *kAllLeagues = "all";

// The leagues offered in the source properties, in display order.
const std::vector<League> &league_catalog();

// nullptr when the slug is not in the catalog.
const League *find_league(const std::string &slug);

// nullptr when no catalog league has this ESPN id.
const League *find_league_by_espn_id(const std::string &espn_id);

// ESPN's flag image for a country code: ".../countries/500/esp.png". Empty for an empty code.
std::string country_flag_url(const std::string &country);

} // namespace fscore
