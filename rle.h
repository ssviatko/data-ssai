#ifndef RLE_H
#define RLE_H

#include <stdexcept>

#include "data.h"

// Run length encoding of the bytes in a data object.
//
// The compressed form is always written in network byte order:
//   uint64  original size in bytes
//   a sequence of packets, each starting with a control byte c:
//     c in 0..127    literal packet: the next c + 1 bytes are copied as they are
//     c in 128..255  run packet: the next byte is repeated c - 126 times (2 to 129)
// The encoder only emits a run packet for 3 or more equal bytes. Data with no runs grows by
// one control byte per 128 bytes (under 1%), plus the 8 byte header.
class rle {
public:
	// returns a new data object holding the compressed form of a_in; a_in is not modified
	static data compress(const data& a_in);

	// returns a new data object holding the original bytes; throws std::runtime_error
	// if a_in is not valid compressed data produced by compress()
	static data decompress(const data& a_in);
};

#endif // RLE_H
