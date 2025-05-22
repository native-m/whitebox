#pragma once

#include <msgpack.h>

#include "fs.h"
#include "stream.h"

namespace wb {

struct MsgpackWriter {
  msgpack_packer pk_{};

  template<IOWriter Writer>
  MsgpackWriter(Writer& writer) {
    msgpack_packer_init(&pk_, &writer, [](void* data, const char* buf, size_t len) {
      Writer* io = (Writer*)data;
      return io->write(buf, len) < len ? -1 : 0;
    });
  }

  template<typename T>
  void write_num(T value) {
    if constexpr (std::same_as<T, int8_t> || std::same_as<T, uint8_t>) {
      msgpack_pack_int8(&pk_, static_cast<int8_t>(value));
    } else if constexpr (std::same_as<T, int16_t> || std::same_as<T, uint16_t>) {
      msgpack_pack_int16(&pk_, static_cast<int16_t>(value));
    } else if constexpr (std::same_as<T, int32_t> || std::same_as<T, uint32_t>) {
      msgpack_pack_int32(&pk_, static_cast<int32_t>(value));
    } else if constexpr (std::same_as<T, int64_t> || std::same_as<T, uint64_t>) {
      msgpack_pack_int64(&pk_, static_cast<int64_t>(value));
    } else if constexpr (std::same_as<T, float>) {
      msgpack_pack_float(&pk_, value);
    } else if constexpr (std::same_as<T, double>) {
      msgpack_pack_double(&pk_, value);
    }
  }

  void write_str(std::string_view str) {
    msgpack_pack_str_with_body(&pk_, str.data(), str.size());
  }

  void write_bool(bool b) {
    if (b) {
      msgpack_pack_true(&pk_);
    } else {
      msgpack_pack_false(&pk_);
    }
  }

  void write_map(uint32_t n) {
    msgpack_pack_map(&pk_, n);
  }

  void write_array(uint32_t n) {
    msgpack_pack_array(&pk_, n);
  }

  void write_kv_num(std::string_view key, auto value) {
    msgpack_pack_str_with_body(&pk_, key.data(), key.size());
    write_num(value);
  }

  void write_kv_str(std::string_view key, std::string_view value) {
    msgpack_pack_str_with_body(&pk_, key.data(), key.size());
    msgpack_pack_str_with_body(&pk_, value.data(), value.size());
  }

  void write_kv_bool(std::string_view key, bool b) {
    msgpack_pack_str_with_body(&pk_, key.data(), key.size());
    write_bool(b);
  }

  void write_kv_map(std::string_view key, uint32_t n) {
    msgpack_pack_str_with_body(&pk_, key.data(), key.size());
    write_map(n);
  }

  void write_kv_array(std::string_view key, uint32_t n) {
    msgpack_pack_str_with_body(&pk_, key.data(), key.size());
    write_array(n);
  }
};

struct MsgpackView {
  msgpack_object obj{};

  bool is_nil() const {
    return obj.type == MSGPACK_OBJECT_NIL;
  }

  bool is_num() const {
    return (obj.type >= MSGPACK_OBJECT_POSITIVE_INTEGER) && (obj.type <= MSGPACK_OBJECT_FLOAT);
  }

  bool is_str() const {
    return obj.type == MSGPACK_OBJECT_STR;
  }

  bool is_bool() const {
    return obj.type == MSGPACK_OBJECT_BOOLEAN;
  }

  bool is_map() const {
    return obj.type == MSGPACK_OBJECT_MAP;
  }

  bool is_array() const {
    return obj.type == MSGPACK_OBJECT_ARRAY;
  }

  template<NumericalType T>
  T as_number(T default_value = T(0)) const {
    switch (obj.type) {
      case MSGPACK_OBJECT_BOOLEAN: return T(obj.via.boolean);
      case MSGPACK_OBJECT_POSITIVE_INTEGER: return T(obj.via.i64);
      case MSGPACK_OBJECT_NEGATIVE_INTEGER: return T(obj.via.u64);
      case MSGPACK_OBJECT_FLOAT64: return T(obj.via.f64);
      case MSGPACK_OBJECT_FLOAT32: return T(obj.via.f64);
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
    const msgpack_object_map& map = obj.via.map;
    for (uint32_t i = 0; i < count; i++) {
      const auto& key = map.ptr[i].key;
      if (key.type == MSGPACK_OBJECT_STR) {
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
  msgpack_unpacked msg_{};

  template<IOReader Reader>
  MsgpackReader(Reader& reader) {
    bytes_ = read_file_content(reader);
    msgpack_unpacked_init(&msg_);
    msgpack_unpack_next(&msg_, (const char*)bytes_.data(), bytes_.size(), nullptr);
  }

  ~MsgpackReader() {
    msgpack_unpacked_destroy(&msg_);
  }

  MsgpackView get_view() {
    return MsgpackView{ msg_.data };
  }
};

}  // namespace wb