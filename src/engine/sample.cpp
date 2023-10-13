#include "sample.h"
#include <sndfile.h>

namespace wb
{
    AudioFormat from_sf_format(int sf_format)
    {
        switch (sf_format) {
            case SF_FORMAT_PCM_16:  return AudioFormat::I16;
            case SF_FORMAT_PCM_24:  return AudioFormat::I24;
            case SF_FORMAT_PCM_32:  return AudioFormat::I32;
            case SF_FORMAT_FLOAT:   return AudioFormat::F32;
            case SF_FORMAT_DOUBLE:  return AudioFormat::F64;
            default:                break;
        }

        return {};
    }

    template<typename T>
    sf_count_t deinterleave_samples(std::vector<std::byte*>& dst, const T* src, sf_count_t num_read, sf_count_t dst_frames, sf_count_t num_frames_written, int channels)
    {
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
        format(other.format),
        channels(other.channels),
        sample_rate(other.sample_rate),
        sample_count(other.sample_count),
        byte_length(other.byte_length),
        sample_data_(std::move(other.sample_data_))
    {
    }

    Sample::~Sample()
    {
        if (sample_data_.size() > 0)
            for (auto channel_data : sample_data_)
                std::free(channel_data);
    }

    std::optional<Sample> Sample::load_file(const std::filesystem::path& path) noexcept
    {
        if (!std::filesystem::is_regular_file(path))
            return {};

        SF_INFO info{};
        SNDFILE* file = sf_open(path.generic_string().c_str(), SFM_READ, &info);
        if (!file)
            return {};

        AudioFormat format = from_sf_format(info.format & SF_FORMAT_SUBMASK);
        if (format == AudioFormat::Unknown)
            return {};

        uint32_t sample_size = get_audio_sample_size(format);
        size_t data_size = info.frames * sample_size;
        std::vector<std::byte*> data;

        // Allocate sample data for each channel
        for (int i = 0; i < info.channels; i++) {
            std::byte* channel_data = (std::byte*)std::malloc(data_size);
            if (!channel_data) {
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
            case AudioFormat::I8:
            {
                WB_ASSERT(false);
                break;
            }
            case AudioFormat::I16:
            {
                int16_t* buffer = (int16_t*)buffer_mem;
                while (num_frames_read = sf_readf_short(file, buffer, buffer_len_per_channel))
                    num_frames_written = deinterleave_samples(data, buffer, num_frames_read, info.frames, num_frames_written, info.channels);
                break;
            }
            case AudioFormat::I24:
            {
                int32_t* buffer = (int32_t*)buffer_mem;
                while (num_frames_read = sf_readf_int(file, buffer, buffer_len_per_channel))
                    num_frames_written = deinterleave_samples(data, buffer, num_frames_read, info.frames, num_frames_written, info.channels);
                break;
            }
            case AudioFormat::I32:
            {
                int32_t* buffer = (int32_t*)buffer_mem;
                while (num_frames_read = sf_readf_int(file, buffer, buffer_len_per_channel))
                    num_frames_written = deinterleave_samples(data, buffer, num_frames_read, info.frames, num_frames_written, info.channels);
                break;
            }
            case AudioFormat::F32:
            {
                float* buffer = (float*)buffer_mem;
                while (num_frames_read = sf_readf_float(file, buffer, buffer_len_per_channel))
                    num_frames_written = deinterleave_samples(data, buffer, num_frames_read, info.frames, num_frames_written, info.channels);
                break;
            }
            case AudioFormat::F64:
                WB_ASSERT(false);
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
        ret->sample_count = info.frames;
        ret->byte_length = data_size * info.channels;
        ret->sample_data_ = std::move(data);

        return ret;
    }

    std::optional<Sample> Sample::copy(const Sample& other) noexcept
    {


        std::optional<Sample> ret;
        ret.emplace();
        ret->name = other.name;
        ret->path = other.path;
        ret->format = other.format;
        ret->channels = other.channels;
        ret->sample_rate = other.sample_rate;
        ret->sample_count = other.sample_count;
        ret->byte_length = other.byte_length;

        return std::optional<Sample>();
    }
}