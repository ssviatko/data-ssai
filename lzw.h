#ifndef LZW_H
#define LZW_H

#include <stdexcept>

#include "data.h"

// Lempel-Ziv-Welch compression of the bytes in a data object.
//
// The compressed form is always written in network byte order:
//   uint64  original size in bytes (nothing else follows if it is 0)
//   a stream of codes packed most significant bit first, last byte zero padded
// Codes 0 to 255 stand for single bytes, code 256 clears the dictionary, and codes from 257
// upward are added to the dictionary as the data is coded. Codes start out 9 bits wide and grow
// one bit at a time, up to 16 bits, as the dictionary fills. When it is full the encoder emits
// a clear code and starts over. No end code is needed, as the decoder stops at the original size.
class lzw {
public:
	// returns a new data object holding the compressed form of a_in; a_in is not modified
	static data compress(const data& a_in);

	// returns a new data object holding the original bytes; throws std::runtime_error
	// if a_in is not valid compressed data produced by compress()
	static data decompress(const data& a_in);
};

#endif // LZW_H
