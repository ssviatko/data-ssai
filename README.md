# `data` class

A growable byte array with a configurable byte order, independent read and write cursors, and typed accessors for integers, floating point values and strings. It is declared in `data.h` and implemented in `data.cc`.

## Building

```
make          # builds ./data-ssai (runs nothing)
./data-ssai   # runs the test cases in main.cc
make clean
```

Requires a C++17 compiler on x86 (see [Platform requirements](#platform-requirements)).

## Concepts

### Storage

Bytes are held in a `std::vector<uint8_t>`. The buffer grows on demand and is always contiguous.

### Byte order

Every multi-byte value (integers, floats, doubles, long doubles, string length prefixes) is encoded according to the instance's current byte order. A new instance defaults to **network order**.

| Value | Meaning |
|---|---|
| `data::NETWORK_ORDER` | Big endian. Most significant byte first. |
| `data::HOST_ORDER` | The machine's native byte order (little endian on x86). |

Changing the byte order affects only subsequent reads and writes. Bytes already in the buffer are never converted. Single bytes (`uint8`) and raw byte operations are unaffected by byte order.

### Cursors

The class keeps two independent 0-based offsets:

- **Write cursor**: where `write_*` operations store data. Valid values are `0` through `size()`. A value equal to `size()` means the next write appends. For a 7 byte buffer, appending corresponds to a write cursor of 7.
- **Read cursor**: where `read_*` operations fetch data. Also `0` through `size()`.

`append_*` ignores the write cursor, always adds at the end, and leaves the write cursor at the new end.

### Errors

| Condition | Exception |
|---|---|
| Setting a cursor beyond `size()` | `std::out_of_range` |
| Reading past the end of the data | `std::out_of_range` |
| String longer than `UINT32_MAX` bytes | `std::length_error` |

A failed read never moves the read cursor.

## Quick example

```cpp
#include "data.h"

data d;                       // network order by default
d.append_uint16(0x1234);      // 12 34
d.append_string("hi");        // 00 00 00 02 68 69
d.append_double(3.5);

d.set_write_cursor(0);
d.write_uint16(0xabcd);       // overwrites the first two bytes

d.read_uint16();              // 0xabcd
d.read_string();              // "hi"
d.read_double();              // 3.5

d.display();                  // hex dump to stdout
```

## Method reference

### Construction

#### `data()`
Creates an empty buffer. Byte order is network, both cursors are 0.

#### `data(const uint8_t *a_bytes, size_t a_len)`
Creates a buffer holding a copy of `a_len` bytes from `a_bytes`. The write cursor starts at the end (`a_len`) and the read cursor at 0.

#### `explicit data(const std::string& a_str)`
Creates a buffer holding a copy of the string's raw bytes. No length prefix is added (unlike `append_string`), and embedded NULs are kept. The write cursor starts at the end and the read cursor at 0.

#### `~data()`
Destroys the instance and its buffer.

### Byte order

#### `void set_byte_order(byte_order a_order)`
Sets the byte order used by all subsequent multi-byte reads and writes. Does not alter existing bytes.

#### `byte_order get_byte_order() const`
Returns the current byte order.

### Cursors

#### `void set_write_cursor(size_t a_pos)`
Moves the write cursor to `a_pos`. Throws `std::out_of_range` if `a_pos > size()`. Use `size()` to position at the end.

#### `size_t get_write_cursor() const`
Returns the write cursor.

#### `void set_read_cursor(size_t a_pos)`
Moves the read cursor to `a_pos`. Throws `std::out_of_range` if `a_pos > size()`.

#### `size_t get_read_cursor() const`
Returns the read cursor.

### Append

All append methods add to the end of the buffer regardless of the write cursor, and leave the write cursor at the new end.

#### `void append(const uint8_t *a_bytes, size_t a_len)`
Appends `a_len` raw bytes. Not affected by byte order.

#### `void append_uint8(uint8_t a_val)`
Appends 1 byte.

#### `void append_uint16(uint16_t a_val)`
Appends 2 bytes in the current byte order.

#### `void append_uint32(uint32_t a_val)`
Appends 4 bytes in the current byte order.

#### `void append_uint64(uint64_t a_val)`
Appends 8 bytes in the current byte order.

#### `void append_arbitrary(uint64_t a_val, unsigned a_bits)`
Appends `a_val` as an unsigned integer `a_bits` wide, in the current byte order. `a_bits` must be a multiple of 8 from 8 to 64, so 24, 40, 48 and 56 bit values are possible as well as the usual widths. Throws `std::invalid_argument` if `a_bits` is not valid or `a_val` does not fit in that many bits; nothing is written in that case.

#### `void append_float(float a_val)`
Appends the 4 byte IEEE 754 binary32 representation in the current byte order.

#### `void append_double(double a_val)`
Appends the 8 byte IEEE 754 binary64 representation in the current byte order.

#### `void append_long_double(long double a_val)`
Appends the 10 byte Intel x87 80-bit extended precision representation: an 8 byte mantissa followed by a 2 byte sign/exponent. The 6 padding bytes that `long double` occupies in memory on x86-64 are not written. In host order the bytes are written as they sit in memory (little endian); in network order all 10 bytes are reversed, so the sign/exponent comes first.

#### `void append_string(const std::string& a_val)`
Appends a `uint32` byte count (in the current byte order) followed by the string's bytes. There is no terminator, so strings may contain embedded NUL bytes. Throws `std::length_error` if the string is longer than `UINT32_MAX` bytes.

#### `void append_random(size_t a_len)`
Appends `a_len` random bytes drawn from `std::random_device`. Byte order does not apply. See [Random bytes](#random-bytes).


### Write at the write cursor

Each write method stores at the write cursor, overwrites any bytes already there, extends the buffer if the value runs past the end, and then advances the write cursor past what it wrote. Encoding is identical to the matching `append_*` method.

#### `void write(const uint8_t *a_bytes, size_t a_len)`
Writes `a_len` raw bytes.

#### `void write_uint8(uint8_t a_val)`
#### `void write_uint16(uint16_t a_val)`
#### `void write_uint32(uint32_t a_val)`
#### `void write_uint64(uint64_t a_val)`
Write 1, 2, 4 or 8 bytes respectively, in the current byte order.

#### `void write_arbitrary(uint64_t a_val, unsigned a_bits)`
Writes an `a_bits` wide unsigned integer, with the same width and range rules and exceptions as `append_arbitrary`.

#### `void write_float(float a_val)`
#### `void write_double(double a_val)`
#### `void write_long_double(long double a_val)`
Write 4, 8 or 10 bytes respectively, in the formats described under the append methods.

#### `void write_string(const std::string& a_val)`
Writes the length prefix and string bytes. Throws `std::length_error` for strings longer than `UINT32_MAX`.

#### `void write_random(size_t a_len)`
Writes `a_len` random bytes at the write cursor, overwriting existing bytes and extending the buffer as `write` does, then advances the write cursor. See [Random bytes](#random-bytes).


### Read at the read cursor

Each read method fetches from the read cursor and advances it past the bytes consumed. Multi-byte values are decoded using the current byte order, so set the order to match how the data was written. If fewer bytes remain than are needed, the method throws `std::out_of_range` and the read cursor does not move.

#### `void read(uint8_t *a_bytes, size_t a_len)`
Copies `a_len` raw bytes into `a_bytes`. The caller supplies a buffer of at least `a_len` bytes.

#### `uint8_t read_uint8()`
#### `uint16_t read_uint16()`
#### `uint32_t read_uint32()`
#### `uint64_t read_uint64()`
Read 1, 2, 4 or 8 bytes respectively.

#### `uint64_t read_arbitrary(unsigned a_bits)`
Reads an `a_bits` wide unsigned integer into a `uint64_t`. `a_bits` must be a multiple of 8 from 8 to 64, otherwise `std::invalid_argument` is thrown. Throws `std::out_of_range`, leaving the read cursor unmoved, if too few bytes remain.

#### `float read_float()`
#### `double read_double()`
#### `long double read_long_double()`
Read 4, 8 or 10 bytes respectively and reconstruct the value. `read_long_double` expects the 10 byte x87 format that `append_long_double` produces.

#### `std::string read_string()`
Reads a `uint32` length prefix, then that many bytes. Throws `std::out_of_range` if either the prefix or the body is truncated, and leaves the read cursor where it started.

### Random bytes

`append_random` and `write_random` take their bytes from `std::random_device`, the standard C++ source of non-deterministic randomness. With GCC and Clang on Linux it reads the kernel's cryptographically secure generator (hardware instructions or `getrandom`), which makes it suitable for keys and nonces there. The C++ standard itself does not guarantee that, so on other platforms check what your library does. A fresh `std::random_device` is created for each call, and the call throws `std::runtime_error` if the device cannot supply bytes. There is no way to seed it, so the output is not reproducible.

### Circular read

These let a `data` act as a first-in first-out queue: append at the end, consume from the front. Each circular read ignores the read cursor and reads from position 0, removes the bytes it read from the front of the buffer, and leaves the read cursor at 0. Values are decoded exactly as by the matching `read_*` method, using the current byte order.

The write cursor moves back by the number of bytes removed (stopping at 0), so it keeps pointing at the same data. If a read throws, nothing changes: the buffer, the read cursor and the write cursor stay as they were. Removing from the front moves the remaining bytes, so each call costs time proportional to `size()`.

#### `void circular_read(uint8_t *a_bytes, size_t a_len)`
#### `uint8_t circular_read_uint8()`
#### `uint16_t circular_read_uint16()`
#### `uint32_t circular_read_uint32()`
#### `uint64_t circular_read_uint64()`
#### `uint64_t circular_read_arbitrary(unsigned a_bits)`
#### `float circular_read_float()`
#### `double circular_read_double()`
#### `long double circular_read_long_double()`
#### `std::string circular_read_string()`
Each behaves like the `read_*` method of the same name (raw bytes, 1, 2, 4 or 8 byte integers, an `a_bits` wide integer, floating point values and a length-prefixed string), including its exceptions: `std::out_of_range` if too few bytes are available and `std::invalid_argument` for a bad `a_bits`.

```cpp
data q;
q.append_uint32(1);
q.append_uint32(2);
q.circular_read_uint32();   // 1; the buffer now holds just the 2
q.append_uint32(3);
q.circular_read_uint32();   // 2
```

### File I/O

These operate on the whole buffer as raw bytes, so byte order does not apply.

#### `void save_file(const std::string& a_path) const`
Writes every byte in the buffer to `a_path`, creating the file or truncating an existing one. Throws `std::runtime_error` if the file cannot be opened or written. Cursors are not affected.

#### `void load_file(const std::string& a_path)`
Replaces the buffer with the contents of the file at `a_path`. Afterwards the write cursor is at the end and the read cursor at 0, as with the byte-copy constructor. Throws `std::runtime_error` if the file cannot be opened or read, in which case the existing data and cursors are unchanged. The byte order is unchanged.

### Inspection and housekeeping

#### `void display(std::ostream& a_os = std::cout) const`
Prints a hex dump of the whole buffer, 16 bytes per row, to `a_os` (standard output by default). Each row shows an 8 digit hex offset, the bytes in hex, and an ASCII column in which non-printable bytes appear as `.`. Empty data prints nothing.

```
00000000  00 01 02 03 04 05 06 07 08 09 0a 0b 0c 0d 0e 0f  |................|
00000010  10                                               |.|
```

#### `void clear()`
Empties the buffer and resets both cursors to 0. The byte order is unchanged.

#### `size_t size() const`
Returns the number of bytes in the buffer.

#### `const std::vector<uint8_t>& bytes() const`
Returns a read-only reference to the underlying bytes. The reference is invalidated by any call that modifies the buffer.

## Huffman compression

`huffman.h` and `huffman.cc` compress the bytes of a `data` object with Huffman coding. The class has two static methods and no state.

#### `static data huffman::compress(const data& a_in)`
Returns a new `data` holding the compressed form of the bytes in `a_in`. The input is not modified. Its byte order and cursors are ignored.

#### `static data huffman::decompress(const data& a_in)`
Returns a new `data` holding the original bytes. Throws `std::runtime_error` if `a_in` is not valid output of `compress` (truncated header or code bits, bad frequency table, or a table that does not match the stored size). The input's byte order and cursors are ignored.

```cpp
#include "huffman.h"

data packed = huffman::compress(original);
data restored = huffman::decompress(packed);   // restored.bytes() == original.bytes()
```

The compressed data is always in network byte order:

| Field | Size | Notes |
|---|---|---|
| Original size | 8 bytes | Nothing follows if 0. |
| Frequency width | 1 byte | Bytes per frequency below, 1 to 8, the smallest that fits the largest frequency. |
| Distinct values | 2 bytes | 1 to 256. |
| Frequency table | per value: 1 byte value + frequency | In ascending value order. |
| Code bits | rest | Most significant bit first, last byte zero padded. Absent if only one distinct value occurs. |

The decoder rebuilds the same code tree from the frequency table, with ties broken deterministically. The table costs a few hundred bytes at most, so very small inputs can come out larger than they went in. Only byte frequencies are used, so data with no skew (random bytes) does not shrink.

## Run length encoding

`rle.h` and `rle.cc` compress the bytes of a `data` object with run length encoding, with the same interface as the other two coders.

#### `static data rle::compress(const data& a_in)`
Returns a new `data` holding the compressed form of the bytes in `a_in`. The input is not modified. Its byte order and cursors are ignored.

#### `static data rle::decompress(const data& a_in)`
Returns a new `data` holding the original bytes. Throws `std::runtime_error` if `a_in` is not valid output of `compress` (truncated header or packet, packets that produce more or fewer bytes than the declared size, trailing data, or a declared size the stream could not possibly produce). The input's byte order and cursors are ignored.

```cpp
#include "rle.h"

data packed = rle::compress(original);
data restored = rle::decompress(packed);   // restored.bytes() == original.bytes()
```

The compressed data is always in network byte order: an 8 byte original size, then packets that each start with a control byte `c`:

| Control byte | Meaning |
|---|---|
| 0 to 127 | Literal packet. The next `c + 1` bytes are copied as they are. |
| 128 to 255 | Run packet. The next byte is repeated `c - 126` times (2 to 129). |

The encoder only writes a run packet for 3 or more equal bytes, since a run of 2 is no smaller than two literals and would split the surrounding literal packet. Data with no runs grows by one control byte per 128 bytes (under 1%) plus the header, so unlike the other coders RLE never expands data badly, but it only helps where long runs of equal bytes occur, such as flat areas in bitmaps. Ordinary text and object code have few runs and barely shrink.

## LZMA compression

`lzma.h` and `lzma.cc` compress the bytes of a `data` object with LZMA. It is written from scratch (range coder, hash-chain match finder and the LZMA literal/match/repeat model), with no external library. The interface mirrors the Huffman class.

#### `static data lzma::compress(const data& a_in)`
Returns a new `data` holding the compressed form of the bytes in `a_in`. The input is not modified. Its byte order and cursors are ignored.

#### `static data lzma::decompress(const data& a_in)`
Returns a new `data` holding the original bytes. Throws `std::runtime_error` if `a_in` is not a valid LZMA alone stream (truncated header or stream, invalid properties, a match distance outside the data decoded so far, or a bad range coder start). The input's byte order and cursors are ignored.

```cpp
#include "lzma.h"

data packed = lzma::compress(original);
data restored = lzma::decompress(packed);   // restored.bytes() == original.bytes()
```

The output is the standard `.lzma` (LZMA alone) format, so other tools can read it, and `decompress` can read streams made by other encoders:

```
$ xz --format=lzma -dc < packed.lzma
```

| Header field | Size | Notes |
|---|---|---|
| Properties | 1 byte | `(pb * 5 + lp) * 9 + lc`. The encoder uses lc 3, lp 0, pb 2 (0x5d). |
| Dictionary size | 4 bytes | The encoder uses the input size rounded up to a power of two, from 4 KiB to 16 MiB. |
| Uncompressed size | 8 bytes | All ones if unknown. |

All header fields are little endian regardless of the data object's byte order. The range coded stream follows. `compress` always records the size and writes no end marker. `decompress` accepts a recorded size or, if the size is all ones, an end marker. Unlike `huffman`, the compressed data is not in network byte order and its layout is fixed by the format.

The encoder uses a greedy parse: a repeat of one of the last four match distances if there is one, otherwise the longest match from the hash chains (up to 48 candidates), then a one byte repeat, then a literal. It does not use the optimal parsing of the reference encoder, so output is typically 4 to 9% larger than `xz` at its default preset on text and binaries. Matches reach back at most the dictionary size. Incompressible data grows by about 1.5%.

## Base64

`base64.h` and `base64.cc` encode the bytes of a `data` object as base64 text and decode them again, with the same shape of interface as the compression classes. This is an encoding, not compression: the text is about a third larger than the input.

#### `static data base64::encode(const data& a_in)`
Returns a new `data` holding the base64 text for the bytes in `a_in`, using the standard alphabet of RFC 4648 section 4 (`A-Z a-z 0-9 + /`) with `=` padding and no line breaks. The result is `4 * ceil(size / 3)` ASCII characters. The input is not modified, and its byte order and cursors are ignored.

#### `static data base64::decode(const data& a_in)`
Returns a new `data` holding the decoded bytes. The decoder is strict and throws `std::runtime_error` unless `a_in` is exactly what `encode` could have produced:
- the length must be a multiple of 4;
- every character must be in the alphabet, so whitespace, line breaks and the URL-safe `-` and `_` are rejected;
- `=` may appear only as the last one or two characters;
- the unused low bits of the last character must be zero, so each input has just one valid encoding.

```cpp
#include "base64.h"

data text = base64::encode(original);          // "Zm9vYmFy" for the bytes "foobar"
data restored = base64::decode(text);          // restored.bytes() == original.bytes()
```

## Platform requirements

`data.cc` contains static assertions that stop the build unless:

- `float` is IEEE 754 binary32 and `double` is binary64.
- `long double` is the x87 80-bit extended type (64 bit mantissa). This holds for GCC/Clang on x86 and x86-64 Linux, but not for ARM64 or MSVC.

## Tests

`main.cc` contains the test cases, in twelve functions called from `main`: `run_tests()` (basic operations, byte order, integers, floats, display), `run_cursor_tests()` (cursors, write-at-cursor, reads), `run_random_tests()` (random bytes), `run_circular_tests()` (circular reads), `run_string_tests()` (strings), `run_arbitrary_tests()` (arbitrary width integers), `run_file_tests()` (file I/O), `run_huffman_tests()` (Huffman compression), `run_lzma_tests()` (LZMA compression), `run_rle_tests()` (run length encoding), `run_base64_tests()` (base64) and `run_comp_dir_tests()` (compression of real files). Each check prints `PASS` or `FAIL`, and the program exits with status 1 if any check fails.

`run_comp_dir_tests()` compresses every regular file in the `comp_test_cases` directory (relative to the working directory, so run `./data-ssai` from the project folder) with each of `rle`, `huffman` and `lzma`, and with `rle` followed by `huffman` or by `lzma` (the RLE output is compressed again). It prints a table of input size and, for each of these five,  the packed size, the packed size as a percentage of input and the ratio input:packed, with a total row. It then checks that every file round trips through every combination (the two chained ones are undone in reverse order). Add files to the directory to include them. If the directory is missing or empty the test prints `SKIP` and does not fail.
