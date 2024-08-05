#include "catch_amalgamated.hpp"
#include "core/algorithm.h"
#include "core/vector.h"
#include <algorithm>

TEST_CASE("find_lower_bound") {
    wb::Vector<int> v;
    v.push_back(1);
    v.push_back(1);
    v.push_back(2);
    v.push_back(2);
    v.push_back(3);
    v.push_back(3);
    v.push_back(3);
    v.push_back(4);
    v.push_back(4);
    v.push_back(4);
    v.push_back(5);
    v.push_back(5);
    v.push_back(6);

    auto it = wb::find_lower_bound(v.begin(), v.end(), 3, [](int a, int b) { return a < b; });
    REQUIRE(*it == 3);
    REQUIRE((it - v.begin()) == 4);

    it = wb::find_lower_bound(v.begin(), v.end(), 3, [](int a, int b) { return a <= b; });
    REQUIRE(*it == 4);
    REQUIRE((it - v.begin()) == 7);
}