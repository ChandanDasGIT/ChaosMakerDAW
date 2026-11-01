#include "testkit.h"

#include "core/version.h"

#include <string>

TEST(version, is_not_empty)
{
    CHECK(!std::string(engineVersion()).empty());
}
