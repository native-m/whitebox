#include "catch_amalgamated.hpp"
#include "core/byte_buffer.h"
#include "core/fs.h"
#include "core/stream.h"
#include "core/vector.h"

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
  REQUIRE(file.open("test.txt", wb::IOOpenMode::Write));
  REQUIRE(file.write_string("Whitebox file I/O test") > 0);
  file.close();
}

TEST_CASE("Regular read") {
  char tmp[23]{};
  wb::File file;
  REQUIRE(file.open("test.txt", wb::IOOpenMode::Read));
  REQUIRE(file.read_string(tmp, 22) > 0);
  REQUIRE(std::strncmp(tmp, "Whitebox file I/O test", 22) == 0);
  file.close();
}

TEST_CASE("Truncate write") {
  wb::File file;
  REQUIRE(file.open("test.txt", wb::IOOpenMode::Write | wb::IOOpenMode::Truncate));
  REQUIRE(file.write_string("Whitebox file I/O test") > 0);
  file.close();

  // The file should match the string size
  REQUIRE(std::filesystem::file_size("test.txt") == 22);

  char tmp[23]{};
  wb::File read_file;
  REQUIRE(read_file.open("test.txt", wb::IOOpenMode::Read));
  REQUIRE(read_file.read_string(tmp, 22) > 0);
  REQUIRE(std::strncmp(tmp, "Whitebox file I/O test", 22) == 0);
  read_file.close();
}

TEST_CASE("Seek file from start") {
  char tmp[23]{};
  wb::File file;
  REQUIRE(file.open("test.txt", wb::IOOpenMode::Read));
  REQUIRE(file.seek(9, wb::IOSeekMode::Begin));
  REQUIRE(file.read_string(tmp, 13) > 0);
  REQUIRE(std::strncmp(tmp, "file I/O test", 13) == 0);
  file.close();
}

TEST_CASE("Seek file from end") {
  char tmp[23]{};
  wb::File file;
  REQUIRE(file.open("test.txt", wb::IOOpenMode::Read));
  REQUIRE(file.seek(-13, wb::IOSeekMode::End));
  REQUIRE(file.read_string(tmp, 13) > 0);
  REQUIRE(std::strncmp(tmp, "file I/O test", 13) == 0);
  file.close();
}
#else
TEST_CASE("Todo") {
  REQUIRE(true);
}
#endif

TEST_CASE("Byte buffer write & read") {
  wb::Vector<int> v;
  for (int i = 0; i < 256; i++)
    v.push_back(i);

  wb::ByteBuffer buf;
  REQUIRE(wb::io_write(buf, 10) == sizeof(int));
  REQUIRE(wb::io_write(buf, 1.2f) == sizeof(float));
  REQUIRE(wb::io_write(buf, v) > 4);
  buf.seek(0, wb::IOSeekMode::Begin);

  int a = 0;
  float b = 0.0f;
  wb::Vector<int> v2;
  REQUIRE(wb::io_read(buf, &a) == sizeof(int));
  REQUIRE(wb::io_read(buf, &b) == sizeof(float));
  REQUIRE(wb::io_read(buf, &v2) > 4);

  REQUIRE(a == 10);
  REQUIRE(b == 1.2f);

  for (int i = 0; i < 256; i++)
    REQUIRE(v2[i] == i);
}