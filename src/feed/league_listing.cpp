#include "feed/league_listing.hpp"

#include "core/espn.hpp"
#include "core/time_util.hpp"

#include <utility>
#include <vector>

namespace fscore {

std::optional<Scoreboard> fetch_league_matches(const std::string &league, int64_t now, const HttpGet &http,
					       CancelToken *cancel)
{
	const CivilDate today = utc_date(now);
	std::vector<std::pair<int, int>> months;
	auto add_month = [&months](int year, int month) {
		if (month < 1) {
			month += 12;
			--year;
		} else if (month > 12) {
			month -= 12;
			++year;
		}
		months.emplace_back(year, month);
	};
	if (today.day <= 3)
		add_month(today.year, today.month - 1);
	add_month(today.year, today.month);
	if (today.day >= 20)
		add_month(today.year, today.month + 1);

	std::optional<Scoreboard> merged;
	for (const auto &[year, month] : months) {
		if (cancel && cancel->cancelled())
			return std::nullopt;
		const HttpResult result = http(espn::month_url(league, year, month), cancel);
		if (!result.ok())
			continue;
		auto board = espn::parse_scoreboard(result.body);
		if (!board)
			continue;
		if (!merged) {
			merged = std::move(board);
			continue;
		}
		if (merged->league_name.empty())
			merged->league_name = board->league_name;
		if (merged->league_logo_url.empty())
			merged->league_logo_url = board->league_logo_url;
		for (Match &match : board->matches)
			merged->matches.push_back(std::move(match));
	}
	return merged;
}

} // namespace fscore
