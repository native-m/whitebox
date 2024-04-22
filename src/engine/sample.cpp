#include "sample.h"
#include <memory>
#include <utility>
#include <sndfile.h>

namespace wb {

AudioFormat from_sf_format(int sf_format) {
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
static sf_count_t deinterleave_samples(std::vector<std::byte*>& dst, const T* src,
                                       sf_count_t num_read, sf_count_t dst_frames,
                                       sf_count_t num_frames_written, int channels) {
    for (int i = 0; i < channels; i++) {
        T* channel_data = (T*)dst[i];
        for (sf_count_t j = 0; j < num_read; j++)
            channel_data[num_frames_written + j] = src[channels * j + i];
    }
    return num_frames_written + num_read;
}

Sample::Sample(Sample&& other) noexcept :
    name(std::move(other.name)),
    path(std::move(other.path)),
    format(std::exchange(other.format, AudioFormat::Unknown)),
    channels(std::exchange(other.channels, 0)),
    sample_rate(std::exchange(other.sample_rate, 0)),
    count(std::exchange(other.count, 0)),
    byte_length(std::exchange(other.byte_length, 0)),
    sample_data(std::move(other.sample_data)) {
}

Sample::~Sample() {
    for (auto sample : sample_data)
        std::free(sample);
}

template <typename T>
static void summarize_for_mipmaps_impl(AudioFormat sample_format, size_t sample_count,
                                       const std::byte* sample_data, size_t chunk_count,
                                       size_t block_count, size_t output_count, T* output_data) {
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

                static constexpr float conv_div_min = (float)std::numeric_limits<T>::min() /
                                                      (float)std::numeric_limits<int8_t>::min();
                static constexpr float conv_div_max = (float)std::numeric_limits<T>::max() /
                                                      (float)std::numeric_limits<int8_t>::max();
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

                static constexpr float conv_div_min = (float)std::numeric_limits<T>::min() /
                                                      (float)std::numeric_limits<int16_t>::min();
                static constexpr float conv_div_max = (float)std::numeric_limits<T>::max() /
                                                      (float)std::numeric_limits<int16_t>::max();
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

                static constexpr double conv_div_min = (double)std::numeric_limits<T>::min() /
                                                       (double)std::numeric_limits<int32_t>::min();
                static constexpr double conv_div_max = (double)std::numeric_limits<T>::max() /
                                                       (double)std::numeric_limits<int32_t>::max();
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
                    float conv =
                        (float)chunk[j] * (chunk[j] >= 0.0f ? std::numeric_limits<T>::max()
                                                            : -std::numeric_limits<T>::min());

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
    }
}

bool Sample::summarize_for_mipmaps(SamplePeaksPrecision precision, uint32_t channel,
                                   uint32_t mip_level, size_t output_offset, size_t* output_count,
                                   void* output_data) const {
    size_t chunk_count = 1ull << mip_level;
    size_t block_count = 1ull << (mip_level - 1);
    size_t mip_data_count = count / block_count;
    mip_data_count += mip_data_count % 2;

    if (output_data == nullptr) {
        *output_count = mip_data_count;
        return true;
    }

    // Output count must be multiple of two
    if ((*output_count & 1) == 1)
        return false;
    if (*output_count < mip_data_count)
        mip_data_count = *output_count;

    std::byte* sample = sample_data[channel];

    switch (precision) {
        case SamplePeaksPrecision::Low:
            summarize_for_mipmaps_impl(format, count, sample, chunk_count, block_count,
                                       mip_data_count, (int8_t*)output_data + output_offset);
            break;
        case SamplePeaksPrecision::High:
            summarize_for_mipmaps_impl(format, count, sample, chunk_count, block_count,
                                       mip_data_count, (int16_t*)output_data + output_offset);
            break;
    }

    return true;
}

std::optional<Sample> Sample::load_file(const std::filesystem::path& path) noexcept {
    if (!std::filesystem::is_regular_file(path))
        return {};

    SF_INFO info;
    SNDFILE* file = sf_open(path.generic_string().c_str(), SFM_READ, &info);
    if (!file)
        return {};

    AudioFormat format = from_sf_format(info.format & SF_FORMAT_SUBMASK);
    if (format == AudioFormat::Unknown)
        return {};

    uint32_t sample_size = get_audio_format_size(format);
    size_t data_size = info.frames * sample_size;
    std::vector<std::byte*> data;

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
    void* buffer_mem = std::malloc(buffer_len_per_channel * info.channels * sample_size);
    if (!buffer_mem) {
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
            int16_t* buffer = (int16_t*)buffer_mem;
            while (num_frames_read = sf_readf_short(file, buffer, buffer_len_per_channel))
                num_frames_written = deinterleave_samples(
                    data, buffer, num_frames_read, info.frames, num_frames_written, info.channels);
            break;
        }
        case AudioFormat::I32: {
            int32_t* buffer = (int32_t*)buffer_mem;
            while (num_frames_read = sf_readf_int(file, buffer, buffer_len_per_channel))
                num_frames_written = deinterleave_samples(
                    data, buffer, num_frames_read, info.frames, num_frames_written, info.channels);
            break;
        }
        case AudioFormat::F32: {
            float* buffer = (float*)buffer_mem;
            while (num_frames_read = sf_readf_float(file, buffer, buffer_len_per_channel))
                num_frames_written = deinterleave_samples(
                    data, buffer, num_frames_read, info.frames, num_frames_written, info.channels);
            break;
        }
        case AudioFormat::F64:
            assert(false && "Not supported at the moment");
            break;
        default:
            break;
    }

    std::free(buffer_mem);
    sf_close(file);

    std::optional<Sample> ret;
    ret.emplace();
    ret->name = path.filename().string();
    ret->path = path;
    ret->format = format;
    ret->channels = info.channels;
    ret->sample_rate = info.samplerate;
    ret->count = info.frames;
    ret->byte_length = data_size * info.channels;
    ret->sample_data = std::move(data);
    return ret;
}

} // namespace wb