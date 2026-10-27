#ifndef LZMA_H
#define LZMA_H

#include <stdexcept>

#include "data.h"

// LZMA compression of the bytes in a data object, implemented from scratch (no external library).
//
// The compressed form is the standard ".lzma" (LZMA alone) stream, so it can be read by xz/lzma
// tools and by any other LZMA decoder. All header fields are little endian regardless of the
// data object's byte order:
//   uint8   properties, (pb * 5 + lp) * 9 + lc; this encoder uses lc 3, lp 0, pb 2
//   uint32  dictionary size
//   uint64  uncompressed size, or all ones if unknown (then the stream ends with an end marker)
//   the range coded stream
// compress() always records the size and writes no end marker. decompress() accepts either form.
class lzma {
public:
	// returns a new data object holding the compressed form of a_in; a_in is not modified
	static data compress(const data& a_in);

	// returns a new data object holding the original bytes; throws std::runtime_error
	// if a_in is not a valid LZMA alone stream
	static data decompress(const data& a_in);
};

#endif // LZMA_H
