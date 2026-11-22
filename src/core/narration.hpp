// Pure core: no OBS, no Qt. ESPN's live commentary (Opta-style English sentences)
// told in Spanish.
#pragma once

#include <optional>
#include <string>

namespace fscore {

// Spanish for one commentary entry ("Foul by Pablo Barrios (Atletico Madrid)." ->
// "Falta de Pablo Barrios (Atletico Madrid)."). nullopt when a sentence in it does not
// follow a known pattern: the caller then says it its own way. Names stay as written.
std::optional<std::string> narrate_es(const std::string &english);

} // namespace fscore
