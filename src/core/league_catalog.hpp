// Pure core: no OBS, no Qt.
#pragma once

#include <string>
#include <vector>

namespace fscore {

struct League {
	const char *slug;    // ESPN league id: "esp.1"
	const char *name;    // "LaLiga"
	const char *country; // ESPN country code for the flag ("esp"); empty for international competitions
};

// The leagues offered in the source properties, in display order.
const std::vector<League> &league_catalog();

// nullptr when the slug is not in the catalog.
const League *find_league(const std::string &slug);

// ESPN's flag image for a country code: ".../countries/500/esp.png". Empty for an empty code.
std::string country_flag_url(const std::string &country);

} // namespace fscore
