#include "catch_amalgamated.hpp"
#include "core/core_math.h"

TEST_CASE("NonLinearDbRange") {
  wb::NonLinearRange db_range(-72.0f, 6.0f, -2.4f);
  INFO("Plain to normalized: " << db_range.plain_to_normalized(0.0f));
  INFO("Normalized to plain: " << db_range.normalized_to_plain(0.0f));
  // CHECK(false);
}
