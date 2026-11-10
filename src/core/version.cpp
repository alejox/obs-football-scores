#include "core/version.hpp"

#ifndef FSCORE_VERSION
#error "FSCORE_VERSION must be defined by the build"
#endif

namespace fscore {

const char *core_version()
{
	return FSCORE_VERSION;
}

} // namespace fscore
