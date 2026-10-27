#include "rle.h"

#include <algorithm>
#include <cstdint>
#include <vector>

namespace {

const unsigned LITERAL_MAX = 128;	// bytes in one literal packet
const unsigned RUN_MIN = 3;	// shortest run worth a run packet
const unsigned RUN_MAX = 129;	// bytes in one run packet

} // namespace

data rle::compress(const data& a_in)
{
	const std::vector<uint8_t>& l_src = a_in.bytes();

	data l_out;
	l_out.set_byte_order(data::NETWORK_ORDER);
	l_out.append_uint64(l_src.size());

	size_t l_lit_start = 0;	// bytes l_src[l_lit_start, l_i) are waiting to be written as literals
	size_t l_i = 0;
	auto l_flush_literals = [&]() {
		while (l_lit_start < l_i) {
			size_t l_len = std::min<size_t>(l_i - l_lit_start, LITERAL_MAX);
			l_out.append_uint8(static_cast<uint8_t>(l_len - 1));
			l_out.append(&l_src[l_lit_start], l_len);
			l_lit_start += l_len;
		}
	};

	while (l_i < l_src.size()) {
		size_t l_run = 1;
		while (l_i + l_run < l_src.size() && l_run < RUN_MAX && l_src[l_i + l_run] == l_src[l_i])
			l_run++;

		if (l_run >= RUN_MIN) {
			l_flush_literals();
			l_out.append_uint8(static_cast<uint8_t>(l_run + 126));
			l_out.append_uint8(l_src[l_i]);
			l_i += l_run;
			l_lit_start = l_i;
		} else {
			l_i++;
		}
	}
	l_flush_literals();

	return l_out;
}

data rle::decompress(const data& a_in)
{
	data l_in(a_in);
	l_in.set_byte_order(data::NETWORK_ORDER);
	l_in.set_read_cursor(0);

	uint64_t l_size;
	try {
		l_size = l_in.read_uint64();
	} catch (const std::out_of_range&) {
		throw std::runtime_error("rle: truncated header");
	}

	const std::vector<uint8_t>& l_bytes = l_in.bytes();
	size_t l_pos = l_in.get_read_cursor();

	// each pair of stream bytes expands to at most RUN_MAX, so reject a size the stream cannot
	// possibly produce before spending time on it
	if (l_size > (l_bytes.size() - l_pos) / 2 * RUN_MAX)
		throw std::runtime_error("rle: declared size is impossible for the amount of data");

	data l_out;
	l_out.set_byte_order(data::NETWORK_ORDER);
	while (l_out.size() < l_size) {
		if (l_pos >= l_bytes.size())
			throw std::runtime_error("rle: truncated data");
		unsigned l_ctrl = l_bytes[l_pos++];
		size_t l_len;
		if (l_ctrl < 128) {
			l_len = l_ctrl + 1;
			if (l_len > l_bytes.size() - l_pos)
				throw std::runtime_error("rle: truncated literal packet");
			if (l_len > l_size - l_out.size())
				throw std::runtime_error("rle: data runs past the declared size");
			l_out.append(&l_bytes[l_pos], l_len);
			l_pos += l_len;
		} else {
			l_len = l_ctrl - 126;
			if (l_pos >= l_bytes.size())
				throw std::runtime_error("rle: truncated run packet");
			if (l_len > l_size - l_out.size())
				throw std::runtime_error("rle: data runs past the declared size");
			uint8_t l_byte = l_bytes[l_pos++];
			for (size_t i = 0; i < l_len; i++)
				l_out.append_uint8(l_byte);
		}
	}
	if (l_pos != l_bytes.size())
		throw std::runtime_error("rle: unexpected data after the declared size");
	return l_out;
}
