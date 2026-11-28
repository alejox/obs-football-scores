#include "core/narration.hpp"

#include <algorithm>
#include <cctype>
#include <regex>
#include <utility>
#include <vector>

namespace fscore {
namespace {

using Rule = std::pair<std::regex, std::string>; // pattern, replacement ($1...)

std::regex re(const char *pattern)
{
	return std::regex(pattern, std::regex::ECMAScript | std::regex::optimize);
}

// Fixed fragments of shot sentences, longest first so "left side of the six yard box"
// wins over "left side of the box".
const std::vector<std::pair<std::string, std::string>> &shot_phrases()
{
	static const std::vector<std::pair<std::string, std::string>> phrases = {
		// Where the shot was taken.
		{" from the left side of the six yard box", " desde la izquierda del área chica"},
		{" from the right side of the six yard box", " desde la derecha del área chica"},
		{" from the centre of the box", " desde el centro del área"},
		{" from the left side of the box", " desde la izquierda del área"},
		{" from the right side of the box", " desde la derecha del área"},
		{" from outside the box", " desde fuera del área"},
		{" from very close range", " desde muy cerca"},
		{" from a difficult angle on the left", " desde un ángulo difícil por la izquierda"},
		{" from a difficult angle on the right", " desde un ángulo difícil por la derecha"},
		{" from a difficult angle and long range on the left", " desde lejos y con poco ángulo por la izquierda"},
		{" from a difficult angle and long range on the right", " desde lejos y con poco ángulo por la derecha"},
		{" from more than 35 yards", " desde más de 30 metros"},
		{" from long range on the left", " desde lejos por la izquierda"},
		{" from long range on the right", " desde lejos por la derecha"},
		{" from a direct free kick", " de tiro libre directo"},
		{" from the penalty spot", " desde el punto penal"},
		// How it ended.
		{" is saved in the bottom left corner", ", atajado abajo a la izquierda"},
		{" is saved in the bottom right corner", ", atajado abajo a la derecha"},
		{" is saved in the top left corner", ", atajado arriba a la izquierda"},
		{" is saved in the top right corner", ", atajado arriba a la derecha"},
		{" is saved in the centre of the goal", ", atajado al centro"},
		{" is saved in the top centre of the goal", ", atajado arriba al centro"},
		{" is high and wide to the left", ", alto y desviado a la izquierda"},
		{" is high and wide to the right", ", alto y desviado a la derecha"},
		{" is close, but misses to the left", ", cerca, pero se va por la izquierda"},
		{" is close, but misses to the right", ", cerca, pero se va por la derecha"},
		{" is close, but misses the top left corner", ", cerca, pero por encima a la izquierda"},
		{" is close, but misses the top right corner", ", cerca, pero por encima a la derecha"},
		{" is just a bit too high", ", apenas por encima"},
		{" is too high", ", se va por arriba"},
		{" misses to the left", ", se va por la izquierda"},
		{" misses to the right", ", se va por la derecha"},
		{" is blocked", ""},
		{" hits the bar", ", pega en el travesaño"},
		{" hits the left post", ", pega en el palo izquierdo"},
		{" hits the right post", ", pega en el palo derecho"},
		{" to the bottom left corner", ", al rincón inferior izquierdo"},
		{" to the bottom right corner", ", al rincón inferior derecho"},
		{" to the top left corner", ", al ángulo superior izquierdo"},
		{" to the top right corner", ", al ángulo superior derecho"},
		{" to the high centre of the goal", ", arriba al centro"},
		{" to the centre of the goal", ", al centro del arco"},
		// Who set it up.
		{" with a cross", " con un centro"},
		{" with a through ball", " con un pase en profundidad"},
		{" with a headed pass", " con un pase de cabeza"},
		{" following a corner", " tras un córner"},
		{" following a set piece situation", " a balón parado"},
		{" following a fast break", " en un contraataque"},
		{" after a corner", " tras un córner"},
		// The keeper who saved it.
		{" by ", " por "},
	};
	return phrases;
}

std::string replace_phrases(std::string text)
{
	for (const auto &[from, to] : shot_phrases()) {
		for (size_t at = text.find(from); at != std::string::npos; at = text.find(from, at + to.size()))
			text.replace(at, from.size(), to);
	}
	return text;
}

const std::vector<Rule> &sentence_rules()
{
	static const std::vector<Rule> rules = {
		{re(R"(^Foul by (.+)\.$)"), "Falta de $1."},
		{re(R"(^(.+) wins a free kick in the defensive half\.$)"), "$1 recibe una falta en su campo."},
		{re(R"(^(.+) wins a free kick in the attacking half\.$)"), "$1 recibe una falta en campo rival."},
		{re(R"(^(.+) wins a free kick on the left wing\.$)"), "$1 recibe una falta por la banda izquierda."},
		{re(R"(^(.+) wins a free kick on the right wing\.$)"), "$1 recibe una falta por la banda derecha."},
		{re(R"(^Corner, (.+)\.$)"), "Córner para $1."},
		{re(R"(^Conceded by (.+)\.$)"), "Lo cede $1."},
		{re(R"(^Substitution, (.+)\.$)"), "Cambio en $1:"},
		{re(R"(^(.+) replaces (.+) because of an injury\.$)"), "entra $1 por $2, lesionado."},
		{re(R"(^(.+) replaces (.+)\.$)"), "entra $1 por $2."},
		{re(R"(^Offside, (.+)\.$)"), "Fuera de juego ($1)."},
		{re(R"(^(.+) tries a through ball, but (.+) is caught offside\.$)"),
		 "$1 busca un pase en profundidad, pero $2 está en fuera de juego."},
		{re(R"(^(.+) is caught offside\.$)"), "$1 queda en fuera de juego."},
		{re(R"(^Back pass by (.+)\.$)"), "Cesión al arquero de $1."},
		{re(R"(^(.+) hits the (?:bar|post|left post|right post) with a (?:right footed shot|left footed shot|header)(.*)\.$)"),
		 "¡Al palo! Remate de $1$2."},
				{re(R"(^(.+) is shown the yellow card for a bad foul\.$)"), "Tarjeta amarilla para $1 por una falta dura."},
		{re(R"(^(.+) is shown the yellow card for hand ball\.$)"), "Tarjeta amarilla para $1 por mano."},
		{re(R"(^(.+) is shown the yellow card for time wasting\.$)"), "Tarjeta amarilla para $1 por perder tiempo."},
		{re(R"(^(.+) is shown the yellow card for a dive\.$)"), "Tarjeta amarilla para $1 por simular."},
		{re(R"(^(.+) is shown the yellow card\.$)"), "Tarjeta amarilla para $1."},
		{re(R"(^(.+) is shown the red card(?: for .+)?\.$)"), "¡Tarjeta roja para $1!"},
		{re(R"(^(.+) is shown the second yellow card(?: for .+)?\.$)"), "¡Segunda amarilla y expulsión para $1!"},
		{re(R"(^Hand ?ball by (.+)\.$)"), "Mano de $1."},
		{re(R"(^Second yellow card to (.+) for (?:a bad foul|.+)\.$)"), "¡Segunda amarilla y expulsión para $1!"},
		{re(R"(^(.+) is shown the yellow card for excessive celebration\.$)"),
		 "Tarjeta amarilla para $1 por celebrar en exceso."},
		{re(R"(^(.+) went off injured after (.+) had used all subs\.$)"),
		 "$1 se retira lesionado; $2 ya no tenía cambios."},
		{re(R"(^Dangerous play by (.+)\.$)"), "Juego peligroso de $1."},
		{re(R"(^Penalty conceded by (.+) after a foul in the penalty area\.$)"),
		 "Penal cometido por $1 por una falta en el área."},
		{re(R"(^Penalty conceded by (.+) with a hand ball in the penalty area\.$)"),
		 "Penal cometido por $1 por mano en el área."},
		{re(R"(^Penalty (.+)\.$)"), "¡Penal a favor de $1!"},
		{re(R"(^(.+) draws a foul in the penalty area\.$)"), "$1 recibe la falta en el área."},
		{re(R"(^(.+) misses the target with a penalty(?:.*)\.$)"), "¡$1 falla el penal!"},
		{re(R"(^Penalty saved! (.+) fails to capitalise on this great opportunity, (.+)\.$)"),
		 "¡Penal atajado! $1 desperdicia la oportunidad, $2."},
		{re(R"(^Delay in match because of an injury (.+)\.$)"), "Juego detenido por la lesión de $1."},
		{re(R"(^Delay in match \((.+)\)\.$)"), "Juego detenido ($1)."},
		{re(R"(^Delay in match\.$)"), "Juego detenido."},
		{re(R"(^Delay over\.$)"), "Se reanuda el juego."},
		{re(R"(^They are ready to continue\.$)"), ""},
		{re(R"(^Lineups are announced and players are warming up\.$)"),
		 "Se anuncian las alineaciones y los jugadores calientan."},
		{re(R"(^First Half begins\.$)"), "Comienza el primer tiempo."},
		{re(R"(^Second Half begins (.+)\.$)"), "Comienza el segundo tiempo: $1."},
		{re(R"(^Second Half begins\.$)"), "Comienza el segundo tiempo."},
		{re(R"(^First Half Extra Time begins (.+)\.$)"), "Comienza la prórroga: $1."},
		{re(R"(^Second Half Extra Time begins (.+)\.$)"), "Comienza el segundo tiempo de la prórroga: $1."},
		{re(R"(^First Half ends, (.+)\.$)"), "Termina el primer tiempo: $1."},
		{re(R"(^Second Half ends, (.+)\.$)"), "Termina el segundo tiempo: $1."},
		{re(R"(^First Half Extra Time ends, (.+)\.$)"), "Termina el primer tiempo de la prórroga: $1."},
		{re(R"(^Second Half Extra Time ends, (.+)\.$)"), "Termina la prórroga: $1."},
		{re(R"(^Match ends, (.+)\.$)"), "Final del partido: $1."},
		{re(R"(^Penalty Shootout begins (.+)\.$)"), "Comienza la tanda de penales: $1."},
		{re(R"(^Penalty Shootout ends, (.+)\.$)"), "Termina la tanda de penales: $1."},
		{re(R"(^Fourth official has announced (\d+) minutes? of added time\.$)"),
		 "El cuarto árbitro anuncia $1 minutos de descuento."},
		{re(R"(^Own Goal by (.+), (.+)\.$)"), "¡Gol en propia puerta de $1 ($2)!"},
		// A bare score line ("Hull City 1, Brighton and Hove Albion 0.") stays as it is.
		{re(R"(^([^.]+ \d+, [^.]+ \d+)\.$)"), "$1."},
		{re(R"(^VAR Decision: (.+)\.$)"), "Decisión del VAR: $1."},
		{re(R"(^Assisted by (.+)\.$)"), "Asistencia de $1."},
		// A penalty goal: "Name (Team) converts the penalty with a left footed shot to ...".
		{re(R"(^(.+?) converts the penalty with a (right footed shot|left footed shot|header)(.*)\.$)"),
		 "SHOT|$1|$2| from the penalty spot$3"},
		// Shots: "Attempt saved. Name (Team) right footed shot from ... is saved in ...".
		{re(R"(^(.+?) (right footed shot|left footed shot|header|with an attempt|shot)(.*)\.$)"), "SHOT|$1|$2|$3"},
	};
	return rules;
}

// One sentence ("...." with its own full stop) in Spanish, or nullopt.
std::optional<std::string> sentence_es(const std::string &sentence, std::string &pending_lead)
{
	// Shot leads come as their own short sentence before the shot itself.
	if (sentence == "Attempt missed.") {
		pending_lead = "missed";
		return std::string();
	}
	if (sentence == "Attempt saved.") {
		pending_lead = "saved";
		return std::string();
	}
	if (sentence == "Attempt blocked.") {
		pending_lead = "blocked";
		return std::string();
	}
	if (sentence.rfind("Goal!", 0) == 0) {
		// "Goal! Hull City 1, Brighton and Hove Albion 0." -> the score comes along.
		pending_lead = "goal|" + sentence.substr(6, sentence.size() - 7);
		return std::string();
	}
	for (const auto &[pattern, replacement] : sentence_rules()) {
		std::smatch match;
		if (!std::regex_match(sentence, match, pattern))
			continue;
		std::string out = match.format(replacement);
		if (out.rfind("SHOT|", 0) == 0) {
			// SHOT|<player (team)>|<kind>|<rest>
			const size_t a = out.find('|', 5), b = out.find('|', a + 1);
			const std::string who = out.substr(5, a - 5);
			const std::string kind = out.substr(a + 1, b - a - 1);
			const std::string rest = replace_phrases(out.substr(b + 1));
			// Any English left in the rest means a fragment we do not know.
			for (const char *word : {" is ", " the ", " misses", " hits", " from ", " to the ", " following"}) {
				if (rest.find(word) != std::string::npos)
					return std::nullopt;
			}
			const bool header = kind == "header";
			const std::string noun = header ? "Cabezazo" : kind == "with an attempt" ? "Intento" : "Remate";
			const std::string foot = kind == "right footed shot" ? " con la derecha"
						 : kind == "left footed shot" ? " con la izquierda"
									      : "";
			if (pending_lead == "missed")
				out = noun + " desviado de " + who + foot + rest + ".";
			else if (pending_lead == "saved")
				out = "¡Atajada! " + noun + " de " + who + foot + rest + ".";
			else if (pending_lead == "blocked")
				out = noun + " bloqueado de " + who + foot + rest + ".";
			else if (pending_lead.rfind("goal|", 0) == 0)
				out = "¡GOOOL! " + pending_lead.substr(5) + ". Gol de " + who + (header ? " de cabeza" : foot) + rest + ".";
			else
				out = noun + " de " + who + foot + rest + ".";
			pending_lead.clear();
		} else {
			out = replace_phrases(out);
		}
		return out;
	}
	return std::nullopt;
}

} // namespace

std::optional<std::string> narrate_es(const std::string &english)
{
	// Split on ". " outside parentheses; names may hold dots ("C. Ronaldo").
	std::vector<std::string> sentences;
	std::string current;
	int depth = 0;
	for (size_t i = 0; i < english.size(); ++i) {
		const char c = english[i];
		current += c;
		if (c == '(')
			++depth;
		else if (c == ')')
			depth = std::max(0, depth - 1);
		else if (c == '.' && depth == 0 && (i + 1 == english.size() || english[i + 1] == ' ')) {
			// "C." in "C. Ronaldo" is an initial: one capital letter before the dot.
			const bool initial = current.size() >= 2 && std::isupper(static_cast<unsigned char>(current[current.size() - 2])) &&
					     (current.size() == 2 || current[current.size() - 3] == ' ');
			if (!initial) {
				while (!current.empty() && current.front() == ' ')
					current.erase(0, 1);
				sentences.push_back(current);
				current.clear();
			}
		}
	}
	while (!current.empty() && current.front() == ' ')
		current.erase(0, 1);
	if (!current.empty())
		sentences.push_back(current + (current.back() == '.' ? "" : "."));
	if (sentences.empty())
		return std::nullopt;

	std::string out, lead;
	for (const std::string &sentence : sentences) {
		auto spanish = sentence_es(sentence, lead);
		if (!spanish)
			return std::nullopt;
		if (spanish->empty())
			continue;
		if (!out.empty())
			out += ' ';
		out += *spanish;
	}
	if (!lead.empty()) {
		// A lead with no shot after it.
		const std::string alone = lead == "missed" ? "Remate desviado." : lead == "saved" ? "¡Atajada!"
					  : lead == "blocked" ? "Remate bloqueado."
							      : "¡GOOOL! " + lead.substr(lead.find('|') + 1) + ".";
		out = out.empty() ? alone : alone + " " + out;
	}
	return out;
}

} // namespace fscore
