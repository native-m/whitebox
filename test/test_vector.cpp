#include "catch_amalgamated.hpp"
#include "core/vector.h"

struct TestType {
  int a = 10;
  TestType() {
  }
  TestType(int v) : a(v) {
  }
  ~TestType() {
    REQUIRE(a != 10);
  }
};

struct TestType2 {
  int a = 10;
  TestType2() {
  }
  TestType2(int v) : a(v) {
  }
  ~TestType2() {
    a = 1;
  }
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

TEST_CASE("Vector push_back and pop_back (trivial)") {
  SECTION("push_back") {
    wb::Vector<int> vec;
    vec.push_back(1);
    vec.push_back(2);
    vec.push_back(3);
    vec.push_back(4);
    vec.push_back(5);
    vec.push_back(6);
    vec.push_back(7);
    vec.push_back(8);
    vec.push_back(9);
    vec.push_back(10);
    REQUIRE(vec[0] == 1);
    REQUIRE(vec[1] == 2);
    REQUIRE(vec[2] == 3);
    REQUIRE(vec[3] == 4);
    REQUIRE(vec[4] == 5);
    REQUIRE(vec[5] == 6);
    REQUIRE(vec[6] == 7);
    REQUIRE(vec[7] == 8);
    REQUIRE(vec[8] == 9);
    REQUIRE(vec[9] == 10);
  }

  SECTION("pop_back") {
    wb::Vector<int> vec;
    vec.push_back(1);
    vec.push_back(2);
    vec.push_back(3);
    REQUIRE(vec.back() == 3);
    vec.pop_back();
    REQUIRE(vec.back() == 2);
    vec.pop_back();
    REQUIRE(vec.back() == 1);
  }
}

TEST_CASE("Vector push_back and pop_back (non-trivial)") {
  SECTION("push_back") {
    wb::Vector<TestType2> vec;
    vec.push_back(TestType2(1));
    vec.push_back(TestType2(2));
    vec.push_back(TestType2(3));
    vec.push_back(TestType2(4));
    vec.push_back(TestType2(5));
    vec.push_back(TestType2(6));
    vec.push_back(TestType2(7));
    vec.push_back(TestType2(8));
    vec.push_back(TestType2(9));
    vec.push_back(TestType2(10));
    REQUIRE(vec[0].a == 1);
    REQUIRE(vec[1].a == 2);
    REQUIRE(vec[2].a == 3);
    REQUIRE(vec[3].a == 4);
    REQUIRE(vec[4].a == 5);
    REQUIRE(vec[5].a == 6);
    REQUIRE(vec[6].a == 7);
    REQUIRE(vec[7].a == 8);
    REQUIRE(vec[8].a == 9);
    REQUIRE(vec[9].a == 10);
  }

  SECTION("pop_back") {
    wb::Vector<TestType2> vec;
    vec.push_back(TestType2(1));
    vec.push_back(TestType2(2));
    vec.push_back(TestType2(3));
    REQUIRE(vec.back().a == 3);
    vec.pop_back();
    REQUIRE(vec.back().a == 2);
    vec.pop_back();
    REQUIRE(vec.back().a == 1);
  }
}

TEST_CASE("Vector emplace_back") {
  SECTION("Trivial") {
    wb::Vector<int> vec;
    vec.emplace_at(0, 1);
    vec.emplace_at(0, 2);
    vec.emplace_at(0, 3);
    vec.emplace_at(1, 4);
    REQUIRE(vec[0] == 3);
    REQUIRE(vec[1] == 4);
    REQUIRE(vec[2] == 2);
    REQUIRE(vec[3] == 1);

    wb::Vector<int> vec2;
    vec2.push_back(1);
    vec2.push_back(2);
    vec2.push_back(3);
    vec2.push_back(4);
    vec2.push_back(5);
    vec2.push_back(6);
    vec2.push_back(7);
    vec2.push_back(8);
    vec2.emplace_at(4, 3);
    REQUIRE(vec2[4] == 3);
  }

  SECTION("Non-trivial") {
    // static_assert(std::movable<TestType2>);
    wb::Vector<TestType2> vec;
    vec.emplace_at(0, 1);
    vec.emplace_at(0, 2);
    vec.emplace_at(0, 3);
    vec.emplace_at(1, 4);
    REQUIRE(vec[0].a == 3);
    REQUIRE(vec[1].a == 4);
    REQUIRE(vec[2].a == 2);
    REQUIRE(vec[3].a == 1);
  }
}

TEST_CASE("Vector reserve & resize") {
  SECTION("Reserve (trivial)") {
    wb::Vector<int> vec;
    vec.reserve(10);
    for (int i = 0; i < 3; i++)
      vec.push_back(i);
    REQUIRE(vec.capacity() == 10);
    REQUIRE(vec.data() != nullptr);
    vec.reserve(20);
    for (int i = 0; i < 3; i++)
      REQUIRE(vec[i] == i);
    REQUIRE(vec.capacity() == 20);
    REQUIRE(vec.data() != nullptr);
    vec.reserve(30);
    for (int i = 0; i < 3; i++)
      REQUIRE(vec[i] == i);
    REQUIRE(vec.capacity() == 30);
    REQUIRE(vec.data() != nullptr);
  }

  SECTION("Resize (trivial)") {
    wb::Vector<int> vec;
    vec.resize(10);
    for (int i = 0; i < 3; i++)
      vec[i] = i;
    REQUIRE(vec.size() == 10);
    REQUIRE(vec.data() != nullptr);
    vec.resize(20);
    for (int i = 0; i < 3; i++)
      REQUIRE(vec[i] == i);
    REQUIRE(vec.size() == 20);
    REQUIRE(vec.data() != nullptr);
    vec.resize(30);
    for (int i = 0; i < 3; i++)
      REQUIRE(vec[i] == i);
    REQUIRE(vec.size() == 30);
    REQUIRE(vec.data() != nullptr);
  }

  SECTION("Reserve (non-trivial)") {
    wb::Vector<TestType2> vec;
    vec.reserve(10);
    for (int i = 0; i < 3; i++)
      vec.push_back(TestType2(i));
    REQUIRE(vec.capacity() == 10);
    REQUIRE(vec.data() != nullptr);
    vec.reserve(20);
    for (int i = 0; i < 3; i++)
      REQUIRE(vec[i].a == i);
    REQUIRE(vec.capacity() == 20);
    REQUIRE(vec.data() != nullptr);
    vec.reserve(30);
    for (int i = 0; i < 3; i++)
      REQUIRE(vec[i].a == i);
    REQUIRE(vec.capacity() == 30);
    REQUIRE(vec.data() != nullptr);
  }

  SECTION("Resize (non-trivial)") {
    wb::Vector<TestType2> vec;
    vec.resize(10);
    for (int i = 0; i < 3; i++)
      vec[i].a = i;
    REQUIRE(vec.size() == 10);
    REQUIRE(vec.data() != nullptr);
    vec.resize(20);
    for (int i = 0; i < 3; i++)
      REQUIRE(vec[i].a == i);
    REQUIRE(vec.size() == 20);
    REQUIRE(vec.data() != nullptr);
    vec.resize(30);
    for (int i = 0; i < 3; i++)
      REQUIRE(vec[i].a == i);
    REQUIRE(vec.size() == 30);
    REQUIRE(vec.data() != nullptr);
  }

  SECTION("Shrink") {
    wb::Vector<TestType2> vec;
    vec.resize(10);
    REQUIRE(vec.size() == 10);
    vec.resize(5);
    REQUIRE(vec.size() == 5);
  }
}
