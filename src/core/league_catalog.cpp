#include "core/league_catalog.hpp"

namespace fscore {

const std::vector<League> &league_catalog()
{
	// Every slug was checked against the live API; the country codes are the
	// ones ESPN uses for its flag images.
	static const std::vector<League> leagues = {
		{"esp.1", "LaLiga", "esp", "740", "https://a.espncdn.com/i/leaguelogos/soccer/500-dark/15.png"},
		{"esp.2", "LaLiga 2", "esp", "3921", "https://a.espncdn.com/i/leaguelogos/soccer/500/107.png"},
		{"esp.copa_del_rey", "Copa del Rey", "esp", "3951", "https://a.espncdn.com/i/leaguelogos/soccer/500-dark/80.png"},
		{"eng.1", "Premier League", "eng", "700", "https://a.espncdn.com/i/leaguelogos/soccer/500-dark/23.png"},
		{"eng.2", "EFL Championship", "eng", "3914", "https://a.espncdn.com/i/leaguelogos/soccer/500-dark/24.png"},
		{"eng.fa", "FA Cup", "eng", "3918", "https://a.espncdn.com/i/leaguelogos/soccer/500-dark/40.png"},
		{"ita.1", "Serie A", "ita", "730", "https://a.espncdn.com/i/leaguelogos/soccer/500-dark/12.png"},
		{"ger.1", "Bundesliga", "ger", "720", "https://a.espncdn.com/i/leaguelogos/soccer/500-dark/10.png"},
		{"fra.1", "Ligue 1", "fra", "710", "https://a.espncdn.com/i/leaguelogos/soccer/500-dark/9.png"},
		{"por.1", "Liga Portugal", "por", "715", "https://a.espncdn.com/i/leaguelogos/soccer/500-dark/14.png"},
		{"ned.1", "Eredivisie", "ned", "725", "https://a.espncdn.com/i/leaguelogos/soccer/500-dark/11.png"},
		{"ksa.1", "Saudi Pro League", "ksa", "21231", "https://a.espncdn.com/i/leaguelogos/soccer/500-dark/2488.png"},
		{"uefa.champions", "UEFA Champions League", "", "775", "https://a.espncdn.com/i/leaguelogos/soccer/500-dark/2.png"},
		{"uefa.europa", "UEFA Europa League", "", "776", "https://a.espncdn.com/i/leaguelogos/soccer/500-dark/2310.png"},
		{"uefa.europa.conf", "UEFA Conference League", "", "20296", "https://a.espncdn.com/i/leaguelogos/soccer/500-dark/20296.png"},
		{"uefa.nations", "UEFA Nations League", "", "2395", "https://a.espncdn.com/i/leaguelogos/soccer/500-dark/2395.png"},
		{"conmebol.libertadores", "CONMEBOL Libertadores", "", "783", "https://a.espncdn.com/i/leaguelogos/soccer/500-dark/58.png"},
		{"conmebol.sudamericana", "CONMEBOL Sudamericana", "", "5454", "https://a.espncdn.com/i/leaguelogos/soccer/500-dark/1208.png"},
		{"col.1", "Liga BetPlay", "col", "650", "https://a.espncdn.com/i/leaguelogos/soccer/500-dark/1543.png"},
		{"arg.1", "Liga Profesional Argentina", "arg", "745", "https://a.espncdn.com/i/leaguelogos/soccer/500-dark/1.png"},
		{"mex.1", "Liga MX", "mex", "760", "https://a.espncdn.com/i/leaguelogos/soccer/500-dark/22.png"},
		{"bra.1", "Brasileirão", "bra", "630", "https://a.espncdn.com/i/leaguelogos/soccer/500-dark/85.png"},
		{"chi.1", "Primera División de Chile", "chi", "640", "https://a.espncdn.com/i/leaguelogos/soccer/500-dark/86.png"},
		{"per.1", "Liga 1 Perú", "per", "670", "https://a.espncdn.com/i/leaguelogos/soccer/500-dark/1813.png"},
		{"ecu.1", "LigaPro Ecuador", "ecu", "660", "https://a.espncdn.com/i/leaguelogos/soccer/500-dark/1944.png"},
		{"uru.1", "Liga AUF Uruguaya", "uru", "680", "https://a.espncdn.com/i/leaguelogos/soccer/500-dark/1592.png"},
		{"usa.1", "MLS", "usa", "770", "https://a.espncdn.com/i/leaguelogos/soccer/500-dark/19.png"},
		{"fifa.world", "FIFA World Cup", "", "606", "https://a.espncdn.com/i/leaguelogos/soccer/500-dark/4.png"},
		{"fifa.worldq.conmebol", "Eliminatorias CONMEBOL", "", "787", "https://a.espncdn.com/i/leaguelogos/soccer/500-dark/65.png"},
		{"fifa.friendly", "Amistosos internacionales", "", "3922", "https://a.espncdn.com/i/leaguelogos/soccer/500-dark/53.png"},
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

const League *find_league_by_espn_id(const std::string &espn_id)
{
	if (espn_id.empty())
		return nullptr;
	for (const League &league : league_catalog()) {
		if (espn_id == league.espn_id)
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
