# nth::cobs

Header-only C++20 implementation of __COBS__ (Consistent Overhead Byte Stuffing) for robust framing of binary byte streams, with full `constexpr` support. The library provides both streaming and one-shot coding, either by using generic callable or directly into a contiguous buffer.

## Usage 

Just copy the `include/nth/cobs.h` into your project directly or add the library with CMake (recommended):

```cmake
add_subdirectory(cobs)
target_link_libraries(your_target PRIVATE cobs)
target_compile_features(your_target PRIVATE cxx_std_20)
```

And include the header:

```cpp
#include <nth/cobs.h>
```

`NTH_COBS_NOINLINE` macro controls whether to annotate all key functions with a noinline attribute, which is disabled by default. To enable noinline, add `-DNTH_COBS_NOINLINE=1` to your build flags. The repository also includes examples of compile-time tests in `main.cpp` that validate encode/decode using `static_assert`, since all the APIs are `constexpr`. You can replicate this test pattern in your own project.

## API Overview

- Free functions:
    - `size_t cobs_encode(std::span<const uint8_t> in, CobsEncodeCb cb)` - One-shot encode via callable.
    - `size_t cobs_encode(std::span<const uint8_t> in, std::span<uint8_t> out)` - One-shot encode into buffer.
    - `size_t cobs_decode(std::span<const uint8_t> in, CobsDecodeCb cb)` - One-shot decode via callable.
    - `size_t cobs_decode(std::span<const uint8_t> in, std::span<uint8_t> out)` - One-shot decode into buffer.

- Streaming encoder `cobs_encoder_t`:
    - `void sink(std::span<const uint8_t> in, CobsEncodeCb cb)` - Feed input fragments.
    - `void stop(CobsEncodeCb cb)` - Emit the final chunk that includes remaining data and trailing `0x00` delimiter.

- Streaming decoder `cobs_decoder_t`:
    - `void sink(std::span<const uint8_t> in, CobsDecodeCb cb)` - Feed encoded fragments. Can optionally accept a trailing `0x00`, then a separate call to `stop()` is not necessary.
    - `void stop(CobsDecodeCb cb)` - Finalize the frame without requiring a delimiter byte. Invokes `cb` once more with `left` indicating validity of last block.

## Examples

### One-shot encode into buffer

```cpp
uint8_t src[] = { /* ... input bytes ... */ };
uint8_t dst[16];

size_t required = nth::cobs_encode(src, dst);

if (required <= sizeof(dst)) {
    // Append delimiter manually if needed
    dst[required++] = 0x00;
} else {
    // Resize buffer to 'required' and retry
}
```

### One-shot encode using a callable

```cpp
uint8_t payload[] = { /* ... input bytes ... */ };
std::vector<uint8_t> frame;

auto write_chunk = [&] (const uint8_t* data, size_t size) 
{
    frame.insert(frame.end(), data, data + size);
};
nth::cobs_encode(payload, write_chunk);
frame.push_back(0x00); // Append delimiter
```

### Streaming encode with `cobs_encoder_t`

```cpp
void write_chunk(const uint8_t* data, size_t size) 
{
    uart_tx(data, size); // Write to serial/socket/etc.
};

uint8_t payload[] = { /* ... payload bytes ... */ };
uint8_t header[] = {
    uint8_t(0x42), // some metadata, e.g. message ID
    uint8_t((sizeof(payload) >> 0) & 0xff),
    uint8_t((sizeof(payload) >> 8) & 0xff), 
};
uint32_t crc = crc32(payload);
uint8_t footer[] = {
    uint8_t((crc >>  0) & 0xff),
    uint8_t((crc >>  8) & 0xff),
    uint8_t((crc >> 16) & 0xff),
    uint8_t((crc >> 24) & 0xff),
};

nth::cobs_encoder_t encoder;
encoder.sink(header, write_chunk);
encoder.sink(payload, write_chunk);
encoder.sink(footer, write_chunk);
encoder.stop(write_chunk); // flush and write delimiter
```

### One-shot decode into buffer

```cpp
uint8_t encoded[] = { /* ... COBS encoded bytes ... */ };
uint8_t decoded[16];

size_t required = nth::cobs_decode(encoded, decoded);
if (required == 0) {
    // malformed input
} else if (required <= sizeof(decoded)) {
    // decoded into buffer; 'required' is the payload size
} else {
    // Resize buffer to 'required' and retry
}
```

### One-shot decode using a callable

```cpp
uint8_t encoded[] = { /* ... COBS encoded bytes ... */ };
std::vector<uint8_t> decoded;

auto write_chunk = [&] (const uint8_t* data, size_t size, size_t left) {
    if (left) {
        // malformed frame, handle as needed
        return;
    }
    decoded.insert(decoded.end(), data, data + size);
};
nth::cobs_decode(encoded, write_chunk);
```

### Streaming decode with `cobs_decoder_t`

```cpp
void write_chunk(const uint8_t* data, size_t size, size_t left)
{
    if (left) {
        // malformed frame, handle as needed
        return;
    }
    process_decoded_bytes(data, size);
};

std::array<uint8_t, 8> frag1{ /* first fragment */ };
std::array<uint8_t, 8> frag2{ /* second fragment */ };
std::array<uint8_t, 8> frag3{ /* optionally includes trailing 0x00 */ };

nth::cobs_decoder_t decoder;
decoder.sink(frag1, write_chunk);
decoder.sink(frag2, write_chunk);
decoder.sink(frag3, write_chunk); // if this does not include 0x00, call stop below
// If no trailing 0x00 delimiter is provided in the stream, finalize explicitly:
decoder.stop(write_chunk);
```
