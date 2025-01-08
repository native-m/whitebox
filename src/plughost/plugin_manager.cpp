#include "plugin_manager.h"
#include "core/byte_buffer.h"
#include "core/debug.h"
#include "core/stream.h"
#include "extern/xxhash.h"
#include "platform/path_def.h"
#include <filesystem>
#include <fmt/ranges.h>
#include <leveldb/db.h>
#include <leveldb/write_batch.h>
#include <pluginterfaces/vst/ivstaudioprocessor.h>
#include <public.sdk/source/vst/hosting/hostclasses.h>
#include <public.sdk/source/vst/hosting/module.h>
#include <public.sdk/source/vst/hosting/plugprovider.h>

namespace ldb = leveldb;
namespace fs = std::filesystem;

namespace wb {
static ldb::DB* plugin_db;
static fs::path vst3_extension {".vst3"};
static Steinberg::Vst::HostApplication vst3_plugin_context;

static PluginInfo decode_plugin_info(ByteBuffer& buffer) {
    PluginInfo info;
    info.descriptor_id.resize(sizeof(VST3::UID::TUID));
    io_read(buffer, &info.structure_version);
    io_read_bytes(buffer, (std::byte*)info.descriptor_id.data(), sizeof(VST3::UID::TUID));
    io_read(buffer, &info.name);
    io_read(buffer, &info.vendor);
    io_read(buffer, &info.version);
    io_read(buffer, &info.path);
    io_read(buffer, &info.flags);
    io_read(buffer, &info.type);
    return info;
}

static void encode_plugin_info(ByteBuffer& buffer, const VST3::UID& descriptor_id, const std::string& name,
                               const std::string& vendor, const std::string& version, const std::string& path,
                               uint32_t flags, PluginType type) {
    io_write(buffer, plugin_info_version);
    io_write_bytes(buffer, (std::byte*)descriptor_id.data(), sizeof(VST3::UID::TUID));
    io_write(buffer, name);
    io_write(buffer, vendor);
    io_write(buffer, version);
    io_write(buffer, path);
    io_write(buffer, flags);
    io_write(buffer, type);
}

void init_plugin_manager() {
    ldb::DB* db;
    ldb::Options options;
    std::string plugin_db_path = (path_def::wbpath / "plugin_db").string();
    options.create_if_missing = true;
    ldb::Status status = ldb::DB::Open(options, plugin_db_path, &db);
    if (!status.ok()) {
        Log::error("Cannot create plugin database {}", status.ToString());
        assert(false);
        return;
    }
    plugin_db = db;
    Steinberg::Vst::PluginContextFactory::instance().setPluginContext(&vst3_plugin_context);
}

Vector<PluginInfo> load_plugin_info() {
    Vector<PluginInfo> plugin_infos;
    ldb::ReadOptions read_options;
    read_options.fill_cache = false;

    ldb::Iterator* iter = plugin_db->NewIterator(read_options);
    for (iter->SeekToFirst(); iter->Valid(); iter->Next()) {
        ldb::Slice value = iter->value();
        ByteBuffer buffer((std::byte*)value.data(), value.size(), false);
        PluginInfo info = decode_plugin_info(buffer);
        std::memcpy(info.plugin_uid, iter->key().data(), 16);
        plugin_infos.push_back(std::move(info));
    }

    return plugin_infos;
}

void scan_vst3_plugins() {
    VST3::Hosting::Module::PathList path_list = VST3::Hosting::Module::getModulePaths();
    ldb::WriteBatch batch;
    std::string error;
    ByteBuffer value_buf;
    char key[sizeof(XXH128_hash_t)] {};

    for (auto& path : path_list) {
        Log::info("Testing VST3 module: {}", path);
        VST3::Hosting::Module::Ptr module = VST3::Hosting::Module::create(path, error);
        if (!module) {
            Log::error("Cannot load VST3 module: {}", path);
            Log::error("Reason: {}", error);
            continue;
        }

        const VST3::Hosting::PluginFactory& factory = module->getFactory();
        for (auto& class_info : factory.classInfos()) {
            if (class_info.category() != kVstAudioEffectClass)
                continue;

            const VST3::UID& id = class_info.ID();
            const auto& subcategories = class_info.subCategories();
            XXH128_hash_t hash = XXH3_128bits(id.data(), sizeof(VST3::UID::TUID)); // Create the key

            // Log information
            Log::info("Name: {}", class_info.name());
            Log::info("Vendor: {}", class_info.vendor());
            Log::info("Version: {}", class_info.version());
            Log::info("Subcategories: {}", fmt::join(subcategories, ", "));

            // Find plugin category
            uint32_t flags = 0;
            for (auto& subcategory : subcategories) {
                if (subcategory == "Fx")
                    flags |= PluginFlags::Effect;
                if (subcategory == "Instrument")
                    flags |= PluginFlags::Instrument;
                if (subcategory == "Analyzer")
                    flags |= PluginFlags::Analyzer;
            }

            value_buf.reset();
            encode_plugin_info(value_buf, id, class_info.name(), class_info.vendor(), class_info.version(), path, flags,
                               PluginType::VST3);
            std::memcpy(key, &hash, sizeof(XXH128_hash_t));
            batch.Put(key, ldb::Slice((char*)value_buf.data(), value_buf.position()));
            Log::info("Added VST3 module: {}", path);
        }
    }

    // Write this into database
    ldb::Status status = plugin_db->Write({}, &batch);
    if (!status.ok())
        Log::error("Cannot store plugin data into database: {}", status.ToString());
}

void scan_plugins() {
    scan_vst3_plugins();
    // scan_XX_plugins...
}

void shutdown_plugin_manager() {
    if (plugin_db)
        delete plugin_db;
}
} // namespace wb