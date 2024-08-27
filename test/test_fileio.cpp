#include "catch_amalgamated.hpp"
#include "core/fs.h"

#ifdef WB_PLATFORM_WINDOWS
class TestRunListener : public Catch::EventListenerBase {
  public:
    using Catch::EventListenerBase::EventListenerBase;
    void testRunStarting(const Catch::TestRunStats&) {
        std::filesystem::remove("test.txt");
    }
};

CATCH_REGISTER_LISTENER(TestRunListener);

TEST_CASE("Regular write") {
    wb::File file;
    REQUIRE(file.open("test.txt", wb::File::Write));
    REQUIRE(file.write_string("Whitebox file I/O test") > 0);
    file.close();
}

TEST_CASE("Regular read") {
    char tmp[23] {};
    wb::File file;
    REQUIRE(file.open("test.txt", wb::File::Read));
    REQUIRE(file.read_string(tmp, 22) > 0);
    REQUIRE(std::strncmp(tmp, "Whitebox file I/O test", 22) == 0);
    file.close();
}

TEST_CASE("Truncate write") {
    wb::File file;
    REQUIRE(file.open("test.txt", wb::File::Write | wb::File::Truncate));
    REQUIRE(file.write_string("Whitebox file I/O test") > 0);
    file.close();

    // The file should match the string size
    REQUIRE(std::filesystem::file_size("test.txt") == 22);

    char tmp[23] {};
    wb::File read_file;
    REQUIRE(read_file.open("test.txt", wb::File::Read));
    REQUIRE(read_file.read_string(tmp, 22) > 0);
    REQUIRE(std::strncmp(tmp, "Whitebox file I/O test", 22) == 0);
    read_file.close();
}

TEST_CASE("Seek file from start") {
    char tmp[23] {};
    wb::File file;
    REQUIRE(file.open("test.txt", wb::File::Read));
    REQUIRE(file.seek(9, wb::File::SeekBegin));
    REQUIRE(file.read_string(tmp, 13) > 0);
    REQUIRE(std::strncmp(tmp, "file I/O test", 13) == 0);
    file.close();
}

TEST_CASE("Seek file from end") {
    char tmp[23] {};
    wb::File file;
    REQUIRE(file.open("test.txt", wb::File::Read));
    REQUIRE(file.seek(-13, wb::File::SeekEnd));
    REQUIRE(file.read_string(tmp, 13) > 0);
    REQUIRE(std::strncmp(tmp, "file I/O test", 13) == 0);
    file.close();
}
#else
TEST_CASE("Todo") {
    REQUIRE(true);
}
#endif