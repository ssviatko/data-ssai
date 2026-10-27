#ifndef HUFFMAN_H
#define HUFFMAN_H

#include <stdexcept>

#include "data.h"

// Huffman coding of the bytes in a data object.
//
// The compressed form is always written in network byte order:
//   uint64  original size in bytes (nothing else follows if it is 0)
//   uint8   width in bytes of each frequency below (1 to 8)
//   uint16  number of distinct byte values (1 to 256)
//   repeated per distinct value, in ascending order: uint8 value, then its
//           frequency as an unsigned integer of the stated width
//   the code bits, most significant bit first, last byte zero padded
//           (absent if only one distinct value occurs, as no bits are needed)
// The decoder rebuilds the identical code tree from the frequencies.
class huffman {
public:
	// returns a new data object holding the compressed form of a_in; a_in is not modified
	static data compress(const data& a_in);

	// returns a new data object holding the original bytes; throws std::runtime_error
	// if a_in is not valid compressed data produced by compress()
	static data decompress(const data& a_in);
};

#endif // HUFFMAN_H
