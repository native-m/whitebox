#include "sample.h"
#include "core/core_math.h"
#include "core/debug.h"
#include "core/defer.h"
#include "extern/dr_mp3.h"
#include <memory>
#include <sndfile.h>
#include <utility>
#include <vorbis/vorbisfile.h>

namespace wb {

static AudioFormat from_sf_format(int sf_format) {
    // Only supports uncompressed format
    switch (sf_format) {
        case SF_FORMAT_PCM_16:
            return AudioFormat::I16;
        case SF_FORMAT_PCM_24:
            return AudioFormat::I32; // Converted to 32-bit
        case SF_FORMAT_PCM_32:
            return AudioFormat::I32;
        case SF_FORMAT_FLOAT:
            return AudioFormat::F32;
        case SF_FORMAT_DOUBLE:
            return AudioFormat::F64;
        default:
            break;
    }

    return {};
}

template <typename T>
static sf_count_t deinterleave_samples(Vector<std::byte*>& dst, const T* src, sf_count_t num_read,
                                       sf_count_t dst_frames, sf_count_t num_frames_written, int channels) {
    for (int i = 0; i < channels; i++) {
        T* channel_data = (T*)dst[i];
        for (sf_count_t j = 0; j < num_read; j++)
            channel_data[num_frames_written + j] = src[channels * j + i];
    }
    return num_frames_written + num_read;
}

template <typename T>
static void summarize_for_mipmaps_impl(AudioFormat sample_format, size_t sample_count, const std::byte* sample_data,
                                       size_t chunk_count, size_t block_count, size_t output_count, T* output_data) {
    switch (sample_format) {
        case AudioFormat::I8: {
            const int8_t* sample = (const int8_t*)sample_data;
            for (size_t i = 0; i < output_count; i += 2) {
                size_t idx = i * block_count;
                size_t chunk_length = std::min(chunk_count, sample_count - idx);
                T min_val = std::numeric_limits<T>::max();
                T max_val = std::numeric_limits<T>::min();
                size_t min_idx = 0;
                size_t max_idx = 0;

                static constexpr float conv_div_min =
                    (float)std::numeric_limits<T>::min() / (float)std::numeric_limits<int8_t>::min();
                static constexpr float conv_div_max =
                    (float)std::numeric_limits<T>::max() / (float)std::numeric_limits<int8_t>::max();
                const int8_t* chunk = &sample[i * block_count];
                for (size_t j = 0; j < chunk_length; j++) {
                    float conv = (float)chunk[j] * (chunk[j] >= 0 ? conv_div_max : conv_div_min);
                    T value = (T)conv;

                    if (value < min_val) {
                        min_val = value;
                        min_idx = j;
                    }
                    if (value > max_val) {
                        max_val = value;
                        max_idx = j;
                    }
                }

                if (max_idx < min_idx) {
                    output_data[i] = max_val;
                    output_data[i + 1] = min_val;
                } else {
                    output_data[i] = min_val;
                    output_data[i + 1] = max_val;
                }
            }
            break;
        }
        case AudioFormat::I16: {
            const int16_t* sample = (const int16_t*)sample_data;
            for (size_t i = 0; i < output_count; i += 2) {
                size_t idx = i * block_count;
                size_t chunk_length = std::min(chunk_count, sample_count - idx);
                T min_val = std::numeric_limits<T>::max();
                T max_val = std::numeric_limits<T>::min();
                size_t min_idx = 0;
                size_t max_idx = 0;

                static constexpr float conv_div_min =
                    (float)std::numeric_limits<T>::min() / (float)std::numeric_limits<int16_t>::min();
                static constexpr float conv_div_max =
                    (float)std::numeric_limits<T>::max() / (float)std::numeric_limits<int16_t>::max();
                const int16_t* chunk = &sample[i * block_count];
                for (size_t j = 0; j < chunk_length; j++) {
                    float conv = (float)chunk[j] * (chunk[j] >= 0 ? conv_div_max : conv_div_min);
                    T value = (T)conv;

                    if (value < min_val) {
                        min_val = value;
                        min_idx = j;
                    }
                    if (value > max_val) {
                        max_val = value;
                        max_idx = j;
                    }
                }

                if (max_idx < min_idx) {
                    output_data[i] = max_val;
                    output_data[i + 1] = min_val;
                } else {
                    output_data[i] = min_val;
                    output_data[i + 1] = max_val;
                }
            }
            break;
        }
        case AudioFormat::I32: {
            const int32_t* sample = (const int32_t*)sample_data;
            for (size_t i = 0; i < output_count; i += 2) {
                size_t idx = i * block_count;
                size_t chunk_length = std::min(chunk_count, sample_count - idx);
                T min_val = std::numeric_limits<T>::max();
                T max_val = std::numeric_limits<T>::min();
                size_t min_idx = 0;
                size_t max_idx = 0;

                static constexpr double conv_div_min =
                    (double)std::numeric_limits<T>::min() / (double)std::numeric_limits<int32_t>::min();
                static constexpr double conv_div_max =
                    (double)std::numeric_limits<T>::max() / (double)std::numeric_limits<int32_t>::max();
                const int32_t* chunk = &sample[i * block_count];
                for (size_t j = 0; j < chunk_length; j++) {
                    double conv = (double)chunk[j] * (chunk[j] >= 0 ? conv_div_max : conv_div_min);
                    T value = (T)conv;

                    if (value < min_val) {
                        min_val = value;
                        min_idx = j;
                    }
                    if (value > max_val) {
                        max_val = value;
                        max_idx = j;
                    }
                }

                if (max_idx < min_idx) {
                    output_data[i] = max_val;
                    output_data[i + 1] = min_val;
                } else {
                    output_data[i] = min_val;
                    output_data[i + 1] = max_val;
                }
            }
            break;
        }
        case AudioFormat::F32: {
            const float* sample = (const float*)sample_data;
            for (size_t i = 0; i < output_count; i += 2) {
                size_t idx = i * block_count;
                size_t chunk_length = std::min(chunk_count, sample_count - idx);
                T min_val = std::numeric_limits<T>::max();
                T max_val = std::numeric_limits<T>::min();
                size_t min_idx = 0;
                size_t max_idx = 0;

                const float* chunk = &sample[i * block_count];
                for (size_t j = 0; j < chunk_length; j++) {
                    float conv = (float)chunk[j] *
                                 (chunk[j] >= 0.0f ? std::numeric_limits<T>::max() : -std::numeric_limits<T>::min());

                    T value = (T)conv;
                    if (value < min_val) {
                        min_val = value;
                        min_idx = j;
                    }
                    if (value > max_val) {
                        max_val = value;
                        max_idx = j;
                    }
                }

                if (max_idx < min_idx) {
                    output_data[i] = max_val;
                    output_data[i + 1] = min_val;
                } else {
                    output_data[i] = min_val;
                    output_data[i + 1] = max_val;
                }
            }
            break;
        }
        default:
            break;
    }
}

Sample::Sample(AudioFormat format, uint32_t sample_rate) : format(format), sample_rate(sample_rate) {
}

Sample::Sample(Sample&& other) noexcept :
    name(std::move(other.name)),
    path(std::move(other.path)),
    format(std::exchange(other.format, AudioFormat::Unknown)),
    channels(std::exchange(other.channels, 0)),
    sample_rate(std::exchange(other.sample_rate, 0)),
    count(std::exchange(other.count, 0)),
    sample_data(std::move(other.sample_data)) {
}

Sample::~Sample() {
    for (auto sample : sample_data)
        std::free(sample);
}

void Sample::set_channel_count(uint32_t count) {

}

void Sample::reserve(size_t new_sample_count) {
}

void Sample::resize(size_t new_sample_count, uint32_t new_channels, bool discard) {
    assert(new_sample_count != 0);
    assert(new_channels != 0);
    if (new_sample_count != count) {
        uint32_t sample_size = get_audio_format_size(format);
        size_t byte_size = new_sample_count * sample_size;
        sample_data.resize(new_channels);
        if (discard || count == 0) {
            for (auto& channel_data : sample_data) {
                if (channel_data)
                    std::free(channel_data);
                channel_data = (std::byte*)std::malloc(byte_size);
                assert(channel_data && "Cannot allocate sample data");
            }
        } else {
            size_t old_byte_size = math::min(count, new_sample_count) * sample_size;
            for (uint32_t i = 0; i < new_channels; i++) {
                void* new_channel_data = std::malloc(byte_size);
                assert(new_channel_data && "Cannot allocate sample data");
                if (sample_data[i]) {
                    std::memcpy(new_channel_data, sample_data[i], old_byte_size);
                    std::free(sample_data[i]);
                }
                sample_data[i] = (std::byte*)new_channel_data;
            }
        }
        channels = new_channels;
        count = new_sample_count;
    } else if (new_channels < channels) {
        for (uint32_t i = new_channels; i < channels; i++)
            std::free(sample_data[i]);
        sample_data.resize(new_channels);
        channels = new_channels;
    } else if (new_channels > channels) {
        uint32_t sample_size = get_audio_format_size(format);
        size_t byte_size = new_sample_count * sample_size;
        sample_data.resize(new_channels);
        for (uint32_t i = channels; i < new_channels; i++)
            sample_data[i] = (std::byte*)std::malloc(byte_size);
        channels = new_channels;
    }
}

std::optional<Sample> Sample::load_file(const std::filesystem::path& path) noexcept {
    if (!std::filesystem::is_regular_file(path))
        return {};

    // Try open with SF
    SF_INFO info;
    SNDFILE* file = sf_open(path.generic_string().c_str(), SFM_READ, &info);
    if (!file)
        return load_compressed_file(path);

    AudioFormat format = from_sf_format(info.format & SF_FORMAT_SUBMASK);
    if (format == AudioFormat::Unknown)
        return {};

    uint32_t sample_size = get_audio_format_size(format);
    size_t data_size = info.frames * sample_size;
    Vector<std::byte*> data;
    data.reserve(info.channels);

    for (int i = 0; i < info.channels; i++) {
        std::byte* channel_data = (std::byte*)std::malloc(data_size);
        if (!channel_data) {
            // Cleanup if failed
            for (auto allocated_data : data)
                std::free(allocated_data);
            sf_close(file);
            return {};
        }
        data.push_back(channel_data);
    }

    sf_count_t buffer_len_per_channel = 1024;
    void* decode_buffer = std::malloc(buffer_len_per_channel * info.channels * sample_size);
    if (!decode_buffer) {
        for (auto allocated_data : data)
            std::free(allocated_data);
        sf_close(file);
        return {};
    }

    sf_count_t num_frames_read = 0;
    sf_count_t num_frames_written = 0;
    switch (format) {
        case AudioFormat::I8: {
            assert(false && "Not supported at the moment");
            break;
        }
        case AudioFormat::I16: {
            int16_t* buffer = (int16_t*)decode_buffer;
            while ((num_frames_read = sf_readf_short(file, buffer, buffer_len_per_channel)))
                num_frames_written =
                    deinterleave_samples(data, buffer, num_frames_read, info.frames, num_frames_written, info.channels);
            break;
        }
        case AudioFormat::I32: {
            int32_t* buffer = (int32_t*)decode_buffer;
            while ((num_frames_read = sf_readf_int(file, buffer, buffer_len_per_channel)))
                num_frames_written =
                    deinterleave_samples(data, buffer, num_frames_read, info.frames, num_frames_written, info.channels);
            break;
        }
        case AudioFormat::F32: {
            float* buffer = (float*)decode_buffer;
            while ((num_frames_read = sf_readf_float(file, buffer, buffer_len_per_channel)))
                num_frames_written =
                    deinterleave_samples(data, buffer, num_frames_read, info.frames, num_frames_written, info.channels);
            break;
        }
        case AudioFormat::F64:
            assert(false && "Not supported at the moment");
            break;
        default:
            break;
    }

    std::free(decode_buffer);
    sf_close(file);

    std::optional<Sample> ret;
    ret.emplace(format, (uint32_t)info.samplerate);
    ret->name = path.filename().string();
    ret->path = path;
    ret->channels = info.channels;
    ret->count = info.frames;
    ret->sample_data = std::move(data);

    return ret;
}

std::optional<Sample> Sample::load_compressed_file(const std::filesystem::path& path) noexcept {
    if (auto mp3 = load_mp3_file(path))
        return mp3;
    if (auto ogv = load_ogg_vorbis_file(path))
        return ogv;
    return {};
}

std::optional<Sample> Sample::load_mp3_file(const std::filesystem::path& path) noexcept {
    if (!std::filesystem::is_regular_file(path))
        return {};

    drmp3 mp3_file;
    if (!drmp3_init_file(&mp3_file, path.generic_string().c_str(), nullptr)) {
        return {};
    }

    uint64_t buffer_len_per_channel = 1024;
    float* decode_buffer = (float*)std::malloc(buffer_len_per_channel * mp3_file.channels * sizeof(float));
    if (!decode_buffer) {
        drmp3_uninit(&mp3_file);
        return {};
    }

    uint64_t total_frame_count = drmp3_get_pcm_frame_count(&mp3_file);
    Vector<std::byte*> channel_samples;
    channel_samples.reserve(mp3_file.channels);

    for (uint32_t i = 0; i < mp3_file.channels; i++) {
        std::byte* mem = (std::byte*)std::malloc(total_frame_count * sizeof(float));
        if (!mem) {
            for (auto sample_data : channel_samples)
                std::free(sample_data);
            std::free(decode_buffer);
            drmp3_uninit(&mp3_file);
            return {};
        }
        channel_samples.push_back(mem);
    }

    uint64_t num_frames_read = 0;
    sf_count_t num_frames_written = 0;
    while (true) {
        num_frames_read = drmp3_read_pcm_frames_f32(&mp3_file, buffer_len_per_channel, decode_buffer);
        if (num_frames_read == 0)
            break;
        num_frames_written = deinterleave_samples(channel_samples, decode_buffer, num_frames_read, total_frame_count,
                                                  num_frames_written, mp3_file.channels);
    }

    drmp3_uninit(&mp3_file);
    std::free(decode_buffer);

    std::optional<Sample> ret;
    ret.emplace(AudioFormat::F32, (uint32_t)mp3_file.sampleRate);
    ret->name = path.filename().string();
    ret->path = path;
    ret->channels = mp3_file.channels;
    ret->count = total_frame_count;
    ret->sample_data = std::move(channel_samples);

    return ret;
}

std::optional<Sample> Sample::load_flac_file(const std::filesystem::path& path) noexcept {
    return std::optional<Sample>();
}

std::optional<Sample> Sample::load_ogg_vorbis_file(const std::filesystem::path& path) noexcept {
    if (!std::filesystem::is_regular_file(path))
        return {};

    OggVorbis_File vf;
    if (ov_fopen(path.generic_string().c_str(), &vf) != 0)
        return {};
    defer(ov_clear(&vf));

    vorbis_info* info = ov_info(&vf, -1);
    int channels = math::min(info->channels, 32); // Maximum number of channel is 32
    uint64_t buffer_len_per_channel = 1024;
    uint64_t total_frame_count = ov_pcm_total(&vf, -1);
    Vector<std::byte*> channel_samples;
    channel_samples.reserve(info->channels);

    for (uint32_t i = 0; i < info->channels; i++) {
        std::byte* mem = (std::byte*)std::malloc(total_frame_count * sizeof(float));
        if (!mem) {
            for (auto sample_data : channel_samples)
                std::free(sample_data);
            return {};
        }
        channel_samples.push_back(mem);
    }

    uint64_t num_frames_written = 0;
    int current_bitstream = 0;
    float** decode_channels = nullptr;
    while (true) {
        int ret = ov_read_float(&vf, &decode_channels, buffer_len_per_channel, &current_bitstream);
        if (ret == 0) {
            break;
        } else if (ret < 0) {
            Log::error("Failed to decode Ogg Vorbis file. ov_read_float() returned {}", ret);
            break;
        }
        for (int c = 0; c < channels; c++) {
            float* channel_data = (float*)channel_samples[c];
            std::memcpy(channel_data + num_frames_written, decode_channels[c], ret * sizeof(float));
        }
        num_frames_written += ret;
    }

    std::optional<Sample> ret;
    ret.emplace(AudioFormat::F32, info->rate);
    ret->name = path.filename().string();
    ret->path = path;
    ret->channels = channels;
    ret->count = total_frame_count;
    ret->sample_data = std::move(channel_samples);

    return ret;
}

std::optional<SampleInfo> Sample::get_file_info(const std::filesystem::path& path) noexcept {
    SF_INFO sf_info {};
    std::string str_path = path.generic_string();
    SNDFILE* file = sf_open(str_path.c_str(), SFM_READ, &sf_info);
    if (file) {
        sf_close(file);
        return SampleInfo {
            .sample_count = (uint64_t)sf_info.frames,
            .channel_count = (uint32_t)sf_info.channels,
            .rate = (uint32_t)sf_info.samplerate,
        };
    }

    drmp3 mp3 {};
    if (drmp3_init_file(&mp3, str_path.c_str(), nullptr)) {
        defer(drmp3_uninit(&mp3));
        return SampleInfo {
            .sample_count = drmp3_get_pcm_frame_count(&mp3),
            .channel_count = mp3.channels,
            .rate = mp3.sampleRate,
        };
    }

    OggVorbis_File vf;
    if (ov_fopen(path.generic_string().c_str(), &vf) == 0) {
        defer(ov_clear(&vf));
        return SampleInfo {
            .sample_count = (uint64_t)ov_pcm_total(&vf, -1),
            .channel_count = (uint32_t)vf.vi->channels,
            .rate = (uint32_t)vf.vi->rate,
        };
    }

    return {};
}

} // namespace wb