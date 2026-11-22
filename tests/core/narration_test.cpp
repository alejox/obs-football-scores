#include "core/narration.hpp"

#include "test_harness.hpp"

#include <string>

using namespace fscore;

namespace {

std::string es(const std::string &english)
{
	return narrate_es(english).value_or("<none>");
}

} // namespace

TEST_CASE(fouls_cards_and_free_kicks)
{
	CHECK_EQ(es("Foul by Koke (Atletico Madrid)."), std::string("Falta de Koke (Atletico Madrid)."));
	CHECK_EQ(es("Pedri (Barcelona) wins a free kick in the attacking half."),
		 std::string("Pedri (Barcelona) recibe una falta en campo rival."));
	CHECK_EQ(es("Koke (Atletico Madrid) is shown the yellow card for a bad foul."),
		 std::string("Tarjeta amarilla para Koke (Atletico Madrid) por una falta dura."));
	CHECK_EQ(es("Second yellow card to Costinha (Brighton and Hove Albion) for a bad foul."),
		 std::string("¡Segunda amarilla y expulsión para Costinha (Brighton and Hove Albion)!"));
	CHECK_EQ(es("Handball by Leif Davis (Ipswich Town)."), std::string("Mano de Leif Davis (Ipswich Town)."));
}

TEST_CASE(shots_saves_and_goals)
{
	CHECK_EQ(es("Attempt saved. Anthony Gordon (Barcelona) right footed shot from the left side of the box is saved in "
		    "the centre of the goal by Jan Oblak (Atletico Madrid). Assisted by Pedri."),
		 std::string("¡Atajada! Remate de Anthony Gordon (Barcelona) con la derecha desde la izquierda del área, "
			     "atajado al centro por Jan Oblak (Atletico Madrid). Asistencia de Pedri."));
	CHECK_EQ(es("Attempt missed. Marc Bernal (Barcelona) left footed shot from outside the box is too high."),
		 std::string("Remate desviado de Marc Bernal (Barcelona) con la izquierda desde fuera del área, se va por arriba."));
	CHECK_EQ(es("Attempt blocked. Koke (Atletico Madrid) header from the centre of the box is blocked. Assisted by "
		    "Pedri with a cross."),
		 std::string("Cabezazo bloqueado de Koke (Atletico Madrid) desde el centro del área. Asistencia de Pedri con un centro."));
	CHECK_EQ(es("Goal! Hull City 1, Brighton and Hove Albion 0. Oliver McBurnie (Hull City) header from very close range "
		    "to the bottom left corner."),
		 std::string("¡GOOOL! Hull City 1, Brighton and Hove Albion 0. Gol de Oliver McBurnie (Hull City) de cabeza desde "
			     "muy cerca, al rincón inferior izquierdo."));
	CHECK_EQ(es("Pedri (Barcelona) hits the bar with a left footed shot from the centre of the box."),
		 std::string("¡Al palo! Remate de Pedri (Barcelona) desde el centro del área."));
}

TEST_CASE(set_pieces_changes_and_match_flow)
{
	CHECK_EQ(es("Corner, Arsenal. Conceded by Lewis Hall."), std::string("Córner para Arsenal. Lo cede Lewis Hall."));
	CHECK_EQ(es("Substitution, Barcelona. Andreas Christensen replaces João Cancelo."),
		 std::string("Cambio en Barcelona: entra Andreas Christensen por João Cancelo."));
	CHECK_EQ(es("Offside, Barcelona. Fermín López is caught offside."),
		 std::string("Fuera de juego (Barcelona). Fermín López queda en fuera de juego."));
	CHECK_EQ(es("Delay in match because of an injury Koke (Atletico Madrid)."),
		 std::string("Juego detenido por la lesión de Koke (Atletico Madrid)."));
	CHECK_EQ(es("Delay over. They are ready to continue."), std::string("Se reanuda el juego."));
	CHECK_EQ(es("First Half ends, Atletico Madrid 1, Barcelona 1."),
		 std::string("Termina el primer tiempo: Atletico Madrid 1, Barcelona 1."));
	CHECK_EQ(es("Fourth official has announced 4 minutes of added time."),
		 std::string("El cuarto árbitro anuncia 4 minutos de descuento."));
	CHECK_EQ(es("Penalty Barcelona. Pedri draws a foul in the penalty area."),
		 std::string("¡Penal a favor de Barcelona! Pedri recibe la falta en el área."));
}

TEST_CASE(names_with_initials_and_unknown_sentences)
{
	CHECK_EQ(es("Foul by C. Ronaldo (Al Nassr)."), std::string("Falta de C. Ronaldo (Al Nassr)."));
	CHECK(!narrate_es("The referee checks the pitch for snow.").has_value());
	CHECK(!narrate_es("").has_value());
}
