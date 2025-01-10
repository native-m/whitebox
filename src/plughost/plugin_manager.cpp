#include "plugin_manager.h"
#include "core/byte_buffer.h"
#include "core/debug.h"
#include "core/stream.h"
#include "extern/xxhash.h"
#include "platform/path_def.h"
#include <algorithm>
#include <filesystem>
#include <fmt/ranges.h>
#include <leveldb/db.h>
#include <leveldb/write_batch.h>
#include <pluginterfaces/vst/ivstaudioprocessor.h>
#include <pluginterfaces/vst/ivsteditcontroller.h>
#include <public.sdk/source/vst/hosting/hostclasses.h>
#include <public.sdk/source/vst/hosting/module.h>
#include <public.sdk/source/vst/hosting/plugprovider.h>
#include <ranges>

namespace ldb = leveldb;
namespace fs = std::filesystem;
namespace vst = Steinberg::Vst;

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
    io_read(buffer, &info.format);
    return info;
}

static void encode_plugin_info(ByteBuffer& buffer, const VST3::UID& descriptor_id, const std::string& name,
                               const std::string& vendor, const std::string& version, const std::string& path,
                               uint32_t flags, PluginFormat format) {
    io_write(buffer, plugin_info_version);
    io_write_bytes(buffer, (std::byte*)descriptor_id.data(), sizeof(VST3::UID::TUID));
    io_write(buffer, name);
    io_write(buffer, vendor);
    io_write(buffer, version);
    io_write(buffer, path);
    io_write(buffer, flags);
    io_write(buffer, format);
}

static void scan_vst3_plugins() {
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
            Steinberg::IPtr<vst::IComponent> component(factory.createInstance<vst::IComponent>(id));
            if (component == nullptr)
                continue; // Skip this class

            if (component->initialize(&vst3_plugin_context) != Steinberg::kResultOk)
                continue;

            uint32_t flags = 0;
            const auto& subcategories = class_info.subCategories();
            XXH128_hash_t hash = XXH3_128bits(id.data(), sizeof(VST3::UID::TUID)); // Create the key
            bool has_audio_input = component->getBusCount(vst::MediaTypes::kAudio, vst::BusDirections::kInput) > 0;
            bool has_audio_output = component->getBusCount(vst::MediaTypes::kAudio, vst::BusDirections::kOutput) > 0;
            bool is_effect = has_audio_output && has_audio_input;
            bool is_instrument =
                has_audio_output && component->getBusCount(vst::MediaTypes::kEvent, vst::BusDirections::kInput) > 0;

            for (auto& subcategory : class_info.subCategories()) {
                if (is_effect && subcategory == "Fx")
                    flags |= PluginFlags::Effect;
                if (is_instrument && subcategory == "Instrument")
                    flags |= PluginFlags::Instrument;
                if (has_audio_input && !has_audio_output && subcategory == "Analyzer")
                    flags |= PluginFlags::Analyzer;
            }

            value_buf.reset();
            encode_plugin_info(value_buf, id, class_info.name(), class_info.vendor(), class_info.version(), path, flags,
                               PluginFormat::VST3);
            std::memcpy(key, &hash, sizeof(XXH128_hash_t));
            batch.Put(ldb::Slice(key, sizeof(XXH128_hash_t)),
                      ldb::Slice((char*)value_buf.data(), value_buf.position()));

            // Log information
            Log::info("Name: {}", class_info.name());
            Log::info("Vendor: {}", class_info.vendor());
            Log::info("Version: {}", class_info.version());
            Log::info("Subcategories: {}", fmt::join(subcategories, ", "));
            Log::info("Added VST3 module: {}", path);

            component->terminate();
        }
    }

    // Write this into database
    ldb::Status status = plugin_db->Write({}, &batch);
    if (!status.ok())
        Log::error("Cannot write plugin data into the database: {}", status.ToString());
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

Vector<PluginInfo> load_plugin_info(const std::string& name_search) {
    Vector<PluginInfo> plugin_infos;
    ldb::ReadOptions read_options;
    read_options.fill_cache = false;

    ldb::Iterator* iter = plugin_db->NewIterator(read_options);
    if (name_search.size() != 0) {
        auto search_lowercase = name_search | std::views::transform([](char ch) { return std::tolower(ch); });
        const std::boyer_moore_horspool_searcher searcher(search_lowercase.begin(), search_lowercase.end());
        for (iter->SeekToFirst(); iter->Valid(); iter->Next()) {
            ldb::Slice value = iter->value();
            ByteBuffer buffer((std::byte*)value.data(), value.size(), false);
            PluginInfo info = decode_plugin_info(buffer);
            auto name_lowercase = info.name | std::views::transform([](char ch) { return std::tolower(ch); });
            if (std::search(name_lowercase.begin(), name_lowercase.end(), searcher) != name_lowercase.end()) {
                std::memcpy(info.uid, iter->key().data(), 16);
                plugin_infos.push_back(std::move(info));
            }
        }
    } else {
        for (iter->SeekToFirst(); iter->Valid(); iter->Next()) {
            ldb::Slice value = iter->value();
            ByteBuffer buffer((std::byte*)value.data(), value.size(), false);
            PluginInfo info = decode_plugin_info(buffer);
            std::memcpy(info.uid, iter->key().data(), 16);
            plugin_infos.push_back(std::move(info));
        }
    }

    delete iter;
    return plugin_infos;
}

void update_plugin_info(const PluginInfo& info) {
    ByteBuffer buffer;
    VST3::UID id = VST3::UID::fromTUID(info.descriptor_id.data());
    encode_plugin_info(buffer, id, info.name, info.vendor, info.version, info.path, info.flags, info.format);
    auto status =
        plugin_db->Put({}, ldb::Slice((char*)info.uid, 16), ldb::Slice((char*)buffer.data(), buffer.position()));
    if (!status.ok()) {
        Log::error("Cannot write plugin data into the database");
        Log::error("Reason: {}", status.ToString());
    }
}

void delete_plugin(uint8_t plugin_uid[16]) {
    ldb::Slice key((char*)plugin_uid, 16);
    auto status = plugin_db->Delete({}, key);
    if (!status.ok()) {
        Log::error("Cannot delete plugin data from the database");
        Log::error("Reason: {}", status.ToString());
    }
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