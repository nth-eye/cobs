/*
 *   Copyright (C) 2025 Ilya Makarov <ilya.makarov.592@gmail.com> (@nth-eye).
 *   All rights reserved.
 */

#ifndef NTH_COBS_H
#define NTH_COBS_H

#ifndef NTH_COBS_NOINLINE
#define NTH_COBS_NOINLINE   0
#endif

#if (NTH_COBS_NOINLINE)
#if defined(__GNUC__) || defined(__clang__)
#define NTH_COBS_NOINLINE_ATTR  __attribute__((noinline))
#elif defined(_MSC_VER)
#define NTH_COBS_NOINLINE_ATTR  __declspec(noinline)
#else
#define NTH_COBS_NOINLINE_ATTR                
#endif
#else
#define NTH_COBS_NOINLINE_ATTR                
#endif

#include <cstdint>
#include <cstddef>
#include <type_traits>
#include <functional>
#include <concepts>
#include <utility>
#include <span>

namespace nth {

/**
 * @brief Concept for a callable that writes encoded COBS output.
 * 
 * @tparam cb Callable to write encoded chunk when ready.
 * @param data Pointer to encoded bytes.
 * @param size Number of encoded bytes.
 */
template<class W>
concept CobsEncodeCb = std::is_invocable_r_v<void, W&, const uint8_t*, size_t>;

/**
 * @brief Streaming COBS encoder with internal buffering.
 *
 * Designed for scenarios where data arrives in fragments and needs to 
 * be encoded on-the-fly without preallocating large output buffers.
 *
 * @note Final chunk includes `0x00` delimiter.
 */
struct cobs_encoder_t {

    /**
     * @brief Reset internal state.
     * 
     */
    constexpr void reset()
    {
        buf[0] = 0;
    }

    /**
     * @brief Sink incoming data using a generic callable.
     * 
     * @param in Input data.
     * @param cb Callable to handle encoded chunk when ready.
     */
    constexpr void sink(std::span<const uint8_t> in, CobsEncodeCb auto&& cb)
    {
        auto& wr = cb;
        for (auto b : in) step(b, wr);
    }

    /**
     * @brief Finalize encoding and write output using a generic callable.
     * 
     * @param cb Callable to handle encoded chunk when ready.
     */
    NTH_COBS_NOINLINE_ATTR constexpr void stop(CobsEncodeCb auto&& cb)
    {
        buf[1 + buf[0]++] = 0;
        std::invoke(cb, buf, buf[0] + 1);
        reset();
    }

protected:

    /**
     * @brief Encode single byte using a generic callable.
     * 
     * @param b Input byte.
     * @param cb Callable to handle encoded chunk when ready.
     */
    NTH_COBS_NOINLINE_ATTR constexpr void step(uint8_t b, CobsEncodeCb auto& cb)
    {
        if (buf[0] == 0xfe)
            flush(cb);
        if (!b)
            flush(cb);
        else
            buf[1 + buf[0]++] = b;
    }

    /**
     * @brief Flush ready chunk to the callable.
     * 
     * @param cb Callable to handle encoded chunk when ready.
     */
    NTH_COBS_NOINLINE_ATTR constexpr void flush(CobsEncodeCb auto& cb)
    {
        std::invoke(cb, buf, ++buf[0]);
        reset();
    }

    uint8_t buf[256] = {};
};

/**
 * @brief Encode with COBS using output callable.
 *
 * Useful when input data is available all at once and needs to be sent to a 
 * non-contiguous storage or directly to a communication interface, e.g. `stdio`. 
 * 
 * @note Does NOT write the final `0x00` delimiter.
 *
 * @param in Input bytes to encode.
 * @param cb Callable to handle encoded chunk when ready.
 * @return Total number of encoded bytes.
 */
NTH_COBS_NOINLINE_ATTR constexpr size_t cobs_encode(std::span<const uint8_t> in, CobsEncodeCb auto&& cb)
{
    const uint8_t* src_dat = in.data();
    const uint8_t* src_end = in.data() + in.size();
    const uint8_t* chunk = in.data();
    uint8_t code = 1;
    size_t total = 0;

    auto write_chunk = [&] (const uint8_t* p) {
        size_t chunk_size = p - chunk;
        std::invoke(cb, &code, 1);
        std::invoke(cb, chunk, chunk_size);
        total += 1 + chunk_size;
    };
    for (; src_dat < src_end; ++src_dat) {
        if (code == 0xff) {
            write_chunk(src_dat); // Flush full block (254 bytes of data)
            chunk = src_dat;
            code = 1;
        }
        if (!*src_dat) {
            write_chunk(src_dat); // Flush current block (may be empty if just flushed above)
            chunk = src_dat + 1;
            code = 1;
        } else {
            ++code;
        }
    }
    write_chunk(src_end);
    return total;
}

/**
 * @brief Encode with COBS directly into output buffer.
 *
 * Useful when input data is available all at once and needs to be stored 
 * in a contiguous memory area. If output buffer is too small, the function 
 * still returns the total required size, but writes only as many bytes as fit.
 * 
 * @note Does NOT write the final `0x00` delimiter.
 *
 * @param in Input bytes to encode.
 * @param out Output buffer.
 * @return Required number of encoded bytes.
 */
NTH_COBS_NOINLINE_ATTR constexpr size_t cobs_encode(std::span<const uint8_t> in, std::span<uint8_t> out) noexcept
{
    const uint8_t* src = in.data();
    const uint8_t* end = in.data() + in.size();
    uint8_t* dst_len = out.data();
    uint8_t* dst_dat = out.data() + 1;
    uint8_t* dst_end = out.data() + out.size();
    uint8_t code = 1;
    size_t required = 1;

    while (src < end) {
        uint8_t b = *src++;
        if (b) {
            if (dst_dat < dst_end)
                *dst_dat++ = b;
            ++code;
            ++required;
        }
        if (code == 0xff || !b) {
            if (dst_len < dst_end)
                dst_len[0] = code;
            dst_len = dst_dat;
            code = 1;
            if (!b || src < end) {
                if (dst_dat < dst_end)
                    dst_dat++;
                ++required;
            }
        }
    }
    if (dst_len < dst_end)
        dst_len[0] = code;
    return required;
}

/**
 * @brief Concept for a callable that writes decoded COBS output.
 * 
 * @tparam W Callable to write decoded chunk when ready.
 * @param data Pointer to decoded bytes.
 * @param size Number of decoded bytes.
 * @param left Number of bytes left to receive if delimiter came prematurely.
 */
template<class W>
concept CobsDecodeCb = std::is_invocable_r_v<void, W&, const uint8_t*, size_t, size_t>;

/**
 * @brief Streaming COBS decoder with internal buffering.
 * 
 * Designed for scenarios where data arrives in fragments and needs to be decoded 
 * on-the-fly without preallocating large output buffers. You may either feed the 
 * terminator `0x00` via `sink()` as part of the input stream, or omit it and call 
 * `stop(cb)` at the end of a frame. On termination, the callback receives one final 
 * chunk with number of missing payload bytes in the last block: `left == 0` for a 
 * well-formed frame, `left > 0` for a malformed or truncated frame. After termination, 
 * the decoder resets and is ready for the next frame.
 * 
 */
struct cobs_decoder_t {

    /**
     * @brief Reset internal state.
     * 
     */
    constexpr void reset()
    {
        size = 0;
        code = 0;
    }

    /**
     * @brief Sink incoming data using a generic callable.
     * 
     * @param in Input data.
     * @param cb Callable to handle decoded chunk when ready.
     */
    constexpr void sink(std::span<const uint8_t> in, CobsDecodeCb auto&& cb)
    {
        auto& wr = cb;
        for (auto b : in) step(b, wr);
    }

    /**
     * @brief Finalize current frame without requiring a delimiter byte.
     *
     * Invokes the callback once with the buffered data and the computed
     * leftover count. After this call the internal state is reset.
     * 
     * @param cb Callable to handle decoded chunk when ready.
     */
    NTH_COBS_NOINLINE_ATTR constexpr void stop(CobsDecodeCb auto&& cb)
    {
        std::invoke(cb, buf, size, code ? code - size - 1u : 0u);
        reset();
    }

protected:

    /**
     * @brief Decode single byte using a generic callable.
     * 
     * @param b Input byte.
     * @param cb Callable to handle decoded chunk when ready.
     */
    NTH_COBS_NOINLINE_ATTR constexpr void step(uint8_t b, CobsDecodeCb auto& cb)
    {
        if (b == 0x00) {
            stop(cb);
            return;
        }
        if (!code || size + 1 == code) {
            if (code && code != 0xff)
                buf[size++] = 0;
            std::invoke(cb, buf, size, 0);
            size = 0;
            code = b;
        } else {
            buf[size++] = b;
        }
    }

    uint8_t size = 0;
    uint8_t code = 0;
    uint8_t buf[255] = {};
};

/**
 * @brief Decode with COBS using output callable.
 *
 * Accepts input with or without a trailing `0x00` delimiter. On termination, the 
 * callback is invoked once for the final chunk with `left` indicating the number 
 * of missing payload bytes in the last block: `left == 0` for a well-formed frame, 
 * `left > 0` for a malformed frame.
 *
 * @param in Input to decode, trailing `0x00` is optional.
 * @param cb Callable to handle decoded chunk when ready.
 * @return Total number of decoded bytes, 0 if malformed input.
 */
NTH_COBS_NOINLINE_ATTR constexpr size_t cobs_decode(std::span<const uint8_t> in, CobsDecodeCb auto&& cb)
{
    const uint8_t* src = in.data();
    const uint8_t* end = in.data() + in.size();
    uint8_t code = 0xff;
    uint8_t block = 0;
    size_t total = 0;

    const uint8_t zero = 0;

    while (src < end) {
        if (block) {
            size_t avail = end - src;
            size_t chunk = block < avail ? block : avail;
            if (chunk) {
                std::invoke(cb, src, chunk, 0u);
                block -= chunk;
                total += chunk;
                src += chunk;
            }
        } else {
            block = *src++;
            if (block && (code != 0xff)) {
                std::invoke(cb, &zero, 1u, 0u);
                ++total;
            }
            code = block;
            if (!code)
                break;
        }
        if (block)
            --block;
    }
    if (block) {
        std::invoke(cb, nullptr, 0u, block);
        return 0u;
    }
    return total;
}

/**
 * @brief Decode with COBS into an output buffer.
 * 
 * Useful when input data is available all at once and needs to be stored 
 * in a contiguous memory area. If output buffer is too small, the function 
 * still returns the total required size, but writes only as many bytes as fit.
 *
 * @param in Input to decode, trailing `0x00` is optional.
 * @param out Output buffer.
 * @return Required number of decoded bytes or 0 if the input is malformed.
 */
NTH_COBS_NOINLINE_ATTR constexpr size_t cobs_decode(std::span<const uint8_t> in, std::span<uint8_t> out) noexcept
{
    const uint8_t* src = in.data();
    const uint8_t* end = in.data() + in.size();
    uint8_t* dst = out.data();
    uint8_t* dst_end = out.data() + out.size();
    uint8_t code = 0xff;
    uint8_t block = 0;
    size_t required = 0;

    while (src < end) {
        if (block) {
            if (dst < dst_end)
                *dst++ = *src;
            ++required;
            ++src;
        } else {
            block = *src++;
            if (block && (code != 0xff)) {
                if (dst < dst_end)
                    *dst++ = 0;
                ++required;
            }
            code = block;
            if (!code)
                break;
        }
        --block;
    }
    return block ? 0u : required;
}

}

#endif
