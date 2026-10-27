#include "base64.h"

#include <cstdint>
#include <vector>

namespace {

const char ALPHABET[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
const char PAD = '=';

// value of each alphabet character, or -1
struct decode_table {
	int8_t value[256];

	decode_table()
	{
		for (int i = 0; i < 256; i++)
			value[i] = -1;
		for (int i = 0; i < 64; i++)
			value[static_cast<uint8_t>(ALPHABET[i])] = static_cast<int8_t>(i);
	}
};

} // namespace

data base64::encode(const data& a_in)
{
	const std::vector<uint8_t>& l_src = a_in.bytes();

	std::vector<uint8_t> l_out;
	l_out.reserve((l_src.size() + 2) / 3 * 4);

	size_t l_i = 0;
	for (; l_i + 3 <= l_src.size(); l_i += 3) {
		uint32_t l_v = (static_cast<uint32_t>(l_src[l_i]) << 16) | (static_cast<uint32_t>(l_src[l_i + 1]) << 8) | l_src[l_i + 2];
		l_out.push_back(ALPHABET[(l_v >> 18) & 63]);
		l_out.push_back(ALPHABET[(l_v >> 12) & 63]);
		l_out.push_back(ALPHABET[(l_v >> 6) & 63]);
		l_out.push_back(ALPHABET[l_v & 63]);
	}

	size_t l_left = l_src.size() - l_i;
	if (l_left) {
		uint32_t l_v = static_cast<uint32_t>(l_src[l_i]) << 16;
		if (l_left == 2)
			l_v |= static_cast<uint32_t>(l_src[l_i + 1]) << 8;
		l_out.push_back(ALPHABET[(l_v >> 18) & 63]);
		l_out.push_back(ALPHABET[(l_v >> 12) & 63]);
		l_out.push_back(l_left == 2 ? ALPHABET[(l_v >> 6) & 63] : PAD);
		l_out.push_back(PAD);
	}

	return data(l_out.data(), l_out.size());
}

data base64::decode(const data& a_in)
{
	static const decode_table l_table;
	const std::vector<uint8_t>& l_src = a_in.bytes();

	if (l_src.size() % 4 != 0)
		throw std::runtime_error("base64: length is not a multiple of 4");

	std::vector<uint8_t> l_out;
	l_out.reserve(l_src.size() / 4 * 3);

	for (size_t l_i = 0; l_i < l_src.size(); l_i += 4) {
		bool l_last = l_i + 4 == l_src.size();
		int l_c[4];
		unsigned l_pads = 0;
		for (int k = 0; k < 4; k++) {
			uint8_t l_ch = l_src[l_i + k];
			if (l_ch == static_cast<uint8_t>(PAD) && l_last && k >= 2) {
				l_c[k] = 0;
				l_pads++;
			} else if (l_pads) {
				throw std::runtime_error("base64: data after padding");
			} else {
				l_c[k] = l_table.value[l_ch];
				if (l_c[k] < 0)
					throw std::runtime_error("base64: invalid character");
			}
		}

		// unused low bits of the last character must be zero, so each input has one encoding
		if (l_pads == 2 && (l_c[1] & 0x0f) != 0)
			throw std::runtime_error("base64: nonzero unused bits");
		if (l_pads == 1 && (l_c[2] & 0x03) != 0)
			throw std::runtime_error("base64: nonzero unused bits");

		uint32_t l_v = (static_cast<uint32_t>(l_c[0]) << 18) | (static_cast<uint32_t>(l_c[1]) << 12) | (static_cast<uint32_t>(l_c[2]) << 6) | static_cast<uint32_t>(l_c[3]);
		l_out.push_back(static_cast<uint8_t>(l_v >> 16));
		if (l_pads < 2)
			l_out.push_back(static_cast<uint8_t>(l_v >> 8));
		if (l_pads < 1)
			l_out.push_back(static_cast<uint8_t>(l_v));
	}

	return data(l_out.data(), l_out.size());
}
