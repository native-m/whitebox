#include "catch_amalgamated.hpp"
#include "core/vector.h"

struct TestType
{
    int a = 10;
    TestType() {}
};

TEST_CASE("Vector construct (trivial type)") {
    SECTION("Empty") {
        wb::Vector<int> vec;
        REQUIRE(vec.size() == 0);
        REQUIRE(vec.capacity() >= 0);
    }

    SECTION("Construct with size") {
        wb::Vector<int> vec(10);
        REQUIRE(vec.size() == 10);
        REQUIRE(vec.capacity() >= 10);
        REQUIRE(vec.data() != nullptr);
        for (int i = 0; i < 10; i++)
            vec[i] = i;
        for (int i = 0; i < 10; i++)
            REQUIRE(vec[i] == i);
    }

    SECTION("Move constructor") {
        wb::Vector<int> vec(10);
        for (int i = 0; i < 10; i++)
            vec[i] = i;

        wb::Vector<int> vec2(std::move(vec));
        REQUIRE(vec2.size() == 10);
        REQUIRE(vec2.capacity() >= 10);
        REQUIRE(vec2.data() != nullptr);
        for (int i = 0; i < 10; i++)
            REQUIRE(vec2[i] == i);
    }
}

TEST_CASE("Vector construct (non-trivial type)") {
    SECTION("Empty") {
        wb::Vector<TestType> vec;
        REQUIRE(vec.size() == 0);
        REQUIRE(vec.capacity() >= 0);
    }

    SECTION("Construct with size") {
        wb::Vector<TestType> vec(5);
        for (int i = 0; i < 5; i++)
            REQUIRE(vec[i].a == 10);
        REQUIRE(vec.size() == 5);
        REQUIRE(vec.capacity() >= 5);
        REQUIRE(vec.data() != nullptr);
        for (int i = 0; i < 5; i++)
            vec[i].a = i;
        for (int i = 0; i < 5; i++)
            REQUIRE(vec[i].a == i);
    }

    SECTION("Move constructor") {
        wb::Vector<TestType> vec(5);
        for (int i = 0; i < 5; i++)
            vec[i].a = i;

        wb::Vector<TestType> vec2(std::move(vec));
        REQUIRE(vec2.size() == 5);
        REQUIRE(vec2.capacity() >= 5);
        REQUIRE(vec2.data() != nullptr);
        for (int i = 0; i < 5; i++)
            REQUIRE(vec2[i].a == i);
    }
}