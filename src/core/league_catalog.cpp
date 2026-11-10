#include "core/league_catalog.hpp"

namespace fscore {

const std::vector<League> &league_catalog()
{
	// Every slug was checked against the live API; the country codes are the
	// ones ESPN uses for its flag images.
	static const std::vector<League> leagues = {
		{"esp.1", "LaLiga", "esp"},
		{"esp.2", "LaLiga 2", "esp"},
		{"esp.copa_del_rey", "Copa del Rey", "esp"},
		{"eng.1", "Premier League", "eng"},
		{"eng.2", "EFL Championship", "eng"},
		{"eng.fa", "FA Cup", "eng"},
		{"ita.1", "Serie A", "ita"},
		{"ger.1", "Bundesliga", "ger"},
		{"fra.1", "Ligue 1", "fra"},
		{"por.1", "Liga Portugal", "por"},
		{"ned.1", "Eredivisie", "ned"},
		{"ksa.1", "Saudi Pro League", "ksa"},
		{"uefa.champions", "UEFA Champions League", ""},
		{"uefa.europa", "UEFA Europa League", ""},
		{"uefa.europa.conf", "UEFA Conference League", ""},
		{"uefa.nations", "UEFA Nations League", ""},
		{"conmebol.libertadores", "CONMEBOL Libertadores", ""},
		{"conmebol.sudamericana", "CONMEBOL Sudamericana", ""},
		{"col.1", "Liga BetPlay", "col"},
		{"arg.1", "Liga Profesional Argentina", "arg"},
		{"mex.1", "Liga MX", "mex"},
		{"bra.1", "Brasileirão", "bra"},
		{"chi.1", "Primera División de Chile", "chi"},
		{"per.1", "Liga 1 Perú", "per"},
		{"ecu.1", "LigaPro Ecuador", "ecu"},
		{"uru.1", "Liga AUF Uruguaya", "uru"},
		{"usa.1", "MLS", "usa"},
		{"fifa.world", "FIFA World Cup", ""},
		{"fifa.worldq.conmebol", "Eliminatorias CONMEBOL", ""},
		{"fifa.friendly", "Amistosos internacionales", ""},
	};
	return leagues;
}

const League *find_league(const std::string &slug)
{
	for (const League &league : league_catalog()) {
		if (slug == league.slug)
			return &league;
	}
	return nullptr;
}

std::string country_flag_url(const std::string &country)
{
	if (country.empty())
		return {};
	return "https://a.espncdn.com/i/teamlogos/countries/500/" + country + ".png";
}

} // namespace fscore
