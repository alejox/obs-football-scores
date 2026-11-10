#include "fixtures.hpp"

#include <fstream>
#include <sstream>
#include <stdexcept>

namespace test_harness {

std::string read_fixture(const std::string &name)
{
	std::ifstream file(std::string(FIXTURE_DIR) + "/" + name, std::ios::binary);
	if (!file)
		throw std::runtime_error("missing fixture " + name);
	std::ostringstream content;
	content << file.rdbuf();
	return content.str();
}

} // namespace test_harness
