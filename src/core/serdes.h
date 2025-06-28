#pragma once

#include <msgpack.hpp>

#include "fs.h"
#include "stream.h"

namespace wb {

struct MsgpackWriter {
  msgpack::packer<File> pk_;

  template<IOWriter Writer>
  MsgpackWriter(Writer& writer) : pk_(writer) {
  }

  template<typename T>
  inline void write_num(T value) {
    if constexpr (std::same_as<T, int8_t> || std::same_as<T, uint8_t>) {
      pk_.pack_int8(static_cast<int8_t>(value));
    } else if constexpr (std::same_as<T, int16_t> || std::same_as<T, uint16_t>) {
      pk_.pack_int16(static_cast<int16_t>(value));
    } else if constexpr (std::same_as<T, int32_t> || std::same_as<T, uint32_t>) {
      pk_.pack_int32(static_cast<int32_t>(value));
    } else if constexpr (std::same_as<T, int64_t> || std::same_as<T, uint64_t>) {
      pk_.pack_int64(static_cast<int64_t>(value));
    } else if constexpr (std::same_as<T, float>) {
      pk_.pack_float(static_cast<float>(value));
    } else if constexpr (std::same_as<T, double>) {
      pk_.pack_double(static_cast<double>(value));
    }
  }

  inline void write_str(std::string_view str) {
    pk_.pack_str(str.size());
    pk_.pack_str_body(str.data(), str.size());
  }

  inline void write_bool(bool b) {
    if (b) {
      pk_.pack_true();
    } else {
      pk_.pack_false();
    }
  }

  inline void write_map(uint32_t n) {
    pk_.pack_map(n);
  }

  inline void write_array(uint32_t n) {
    pk_.pack_array(n);
  }

  inline void write_kv_num(std::string_view key, auto value) {
    write_str(key);
    write_num(value);
  }

  inline void write_kv_str(std::string_view key, std::string_view value) {
    write_str(key);
    write_str(value);
  }

  inline void write_kv_bool(std::string_view key, bool b) {
    write_str(key);
    write_bool(b);
  }

  inline void write_kv_map(std::string_view key, uint32_t n) {
    write_str(key);
    write_map(n);
  }

  inline void write_kv_array(std::string_view key, uint32_t n) {
    write_str(key);
    write_array(n);
  }
};

struct MsgpackView {
  msgpack::object obj{};

  bool is_nil() const {
    return obj.type == msgpack::type::NIL;
  }

  bool is_num() const {
    return (obj.type >= msgpack::type::POSITIVE_INTEGER) && (obj.type <= msgpack::type::FLOAT);
  }

  bool is_str() const {
    return obj.type == msgpack::type::STR;
  }

  bool is_bool() const {
    return obj.type == msgpack::type::BOOLEAN;
  }

  bool is_map() const {
    return obj.type == msgpack::type::MAP;
  }

  bool is_array() const {
    return obj.type == msgpack::type::ARRAY;
  }

  template<NumericalType T>
  T as_number(T default_value = T(0)) const {
    switch (obj.type) {
      case msgpack::type::BOOLEAN: return T(obj.via.boolean);
      case msgpack::type::POSITIVE_INTEGER: return T(obj.via.i64);
      case msgpack::type::NEGATIVE_INTEGER: return T(obj.via.u64);
      case msgpack::type::FLOAT64: return T(obj.via.f64);
      case msgpack::type::FLOAT32: return T(obj.via.f64);
      default: break;
    }
    return default_value;
  }

  std::string_view as_str(std::string_view default_value = "") const {
    if (is_str()) {
      return std::string_view(obj.via.str.ptr, obj.via.str.size);
    }
    return default_value;
  }

  bool as_bool(bool default_value = false) const {
    if (is_bool()) {
      return obj.via.boolean;
    }
    return default_value;
  }

  uint32_t array_size() const {
    if (is_array()) {
      return obj.via.array.size;
    }
    return 0;
  }

  MsgpackView array_get(uint32_t n) {
    assert(is_array() && "The object is not an array");
    assert(n < obj.via.array.size);
    return { obj.via.array.ptr[n] };
  }

  MsgpackView map_find(std::string_view view) {
    assert(is_map() && "The object is not a map");
    uint32_t count = obj.via.map.size;
    const msgpack::object_map& map = obj.via.map;
    for (uint32_t i = 0; i < count; i++) {
      const auto& key = map.ptr[i].key;
      if (key.type == msgpack::type::STR) {
        std::string_view key_str(key.via.str.ptr, key.via.str.size);
        if (key_str == view) {
          return { map.ptr[i].val };
        }
      }
    }
    return {};
  }

  operator bool() {
    return !is_nil();
  }
};

struct MsgpackReader {
  Vector<std::byte> bytes_;
  msgpack::object_handle result_{};

  template<IOReader Reader>
  MsgpackReader(Reader& reader) {
    bytes_ = read_file_content(reader);
    result_ = msgpack::unpack((const char*)bytes_.data(), bytes_.size());
  }

  ~MsgpackReader() {
  }

  MsgpackView get_view() {
    return MsgpackView{ result_.get() };
  }
};

}  // namespace wb