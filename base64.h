#ifndef BASE64_H
#define BASE64_H

#include <stdexcept>

#include "data.h"

// Base64 encoding of the bytes in a data object, as in RFC 4648 section 4: the alphabet
// A-Z a-z 0-9 + / with = padding. The encoded form is plain ASCII text with no line breaks,
// 4 characters for every 3 bytes (rounded up), and does not depend on any byte order.
class base64 {
public:
	// returns a new data object holding the base64 text for a_in; a_in is not modified
	static data encode(const data& a_in);

	// returns a new data object holding the decoded bytes; throws std::runtime_error if a_in is not
	// strictly valid base64: a length that is not a multiple of 4, a character outside the alphabet
	// (this includes whitespace and the URL-safe - and _), padding anywhere but the end of the last
	// group, or unused bits in the last character that are not zero
	static data decode(const data& a_in);
};

#endif // BASE64_H
