#ifndef STREAM_UNPACKER_H
#define STREAM_UNPACKER_H

#include "../base/BaseTypes.h"
#include <assert.h>
#include <vector>

namespace bitwave {
namespace net {

    // a template class use UnpackRuler to unpack tcp stream data, if unpack
    // success, then call the OnUnpackOne function
    template<typename UnpackRuler, int base_buffer_size = 2048>
    class StreamUnpacker : private NotCopyable
    {
    public:
        typedef std::vector<char> Stream;

        StreamUnpacker()
            : stream_buffer_()
        {
            stream_buffer_.reserve(base_buffer_size);
        }

        // Tcp stream data arrival, then call this function to unpack data
        void StreamDataArrive(const char *data, std::size_t size)
        {
            assert(data);
            stream_buffer_.insert(stream_buffer_.end(), data, data + size);

            std::size_t pack_start = 0;
            while (true)
            {
                std::size_t pack_len = 0;
                std::size_t pack_remain = stream_buffer_.size() - pack_start;
                if (pack_remain == 0) break;

                bool is_can_unpack = UnpackRuler::CanUnpack(
                        &stream_buffer_[pack_start],
                        pack_remain, &pack_len);
                if (!is_can_unpack) break;

                OnUnpackOne(&stream_buffer_[pack_start], pack_len);
                pack_start += pack_len;
            }

            Stream::iterator unpacked_begin = stream_buffer_.begin();
            Stream::iterator unpacked_end = unpacked_begin + pack_start;
            stream_buffer_.erase(unpacked_begin, unpacked_end);
        }

        // clear tcp stream buffer
        void Clear()
        {
            stream_buffer_.clear();
        }

    private:
        virtual void OnUnpackOne(const char *data, std::size_t size) = 0;

        Stream stream_buffer_;
    };

} // namespace net
} // namespace bitwave

#endif // STREAM_UNPACKER_H
