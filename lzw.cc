#include "lzw.h"

#include <algorithm>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace {

const unsigned MIN_BITS = 9;
const unsigned MAX_BITS = 16;
const uint32_t CLEAR = 256;
const uint32_t FIRST_FREE = 257;
const uint32_t DICT_MAX = 1u << MAX_BITS;	// number of codes

const size_t HEADER_SIZE = 8;

// width of the code that is read or written when a_next codes are in the dictionary
unsigned code_bits(uint32_t a_next)
{
	unsigned l_bits = MIN_BITS;
	while (l_bits < MAX_BITS && (a_next - 1) >> l_bits)
		l_bits++;
	return l_bits;
}

class bit_writer {
public:
	explicit bit_writer(std::vector<uint8_t>& a_out)
		: m_out(a_out)
	{
	}

	void put(uint32_t a_code, unsigned a_bits)
	{
		m_acc = (m_acc << a_bits) | a_code;
		m_count += a_bits;
		while (m_count >= 8) {
			m_count -= 8;
			m_out.push_back(static_cast<uint8_t>(m_acc >> m_count));
		}
		m_acc &= (1u << m_count) - 1;
	}

	void flush()
	{
		if (m_count)
			put(0, 8 - m_count);
	}

private:
	std::vector<uint8_t>& m_out;
	uint32_t m_acc = 0;
	unsigned m_count = 0;
};

class bit_reader {
public:
	bit_reader(const uint8_t *a_p, size_t a_n)
		: m_p(a_p), m_end(a_p + a_n)
	{
	}

	uint32_t get(unsigned a_bits)
	{
		while (m_count < a_bits) {
			if (m_p == m_end)
				throw std::runtime_error("lzw: truncated data");
			m_acc = (m_acc << 8) | *m_p++;
			m_count += 8;
		}
		m_count -= a_bits;
		uint32_t l_code = (m_acc >> m_count) & ((1u << a_bits) - 1);
		m_acc &= (1u << m_count) - 1;
		return l_code;
	}

	// true if only zero padding is left
	bool done() const
	{
		return m_p == m_end && m_count < 8 && m_acc == 0;
	}

private:
	const uint8_t *m_p;
	const uint8_t *m_end;
	uint32_t m_acc = 0;
	unsigned m_count = 0;
};

} // namespace

data lzw::compress(const data& a_in)
{
	const std::vector<uint8_t>& l_src = a_in.bytes();

	std::vector<uint8_t> l_out;
	for (int i = 7; i >= 0; i--)
		l_out.push_back(static_cast<uint8_t>(static_cast<uint64_t>(l_src.size()) >> (i * 8)));

	if (!l_src.empty()) {
		bit_writer l_bw(l_out);
		std::unordered_map<uint32_t, uint32_t> l_dict;	// (prefix code << 8 | byte) -> code
		uint32_t l_next = FIRST_FREE;
		uint32_t l_w = l_src[0];

		for (size_t i = 1; i < l_src.size(); i++) {
			uint32_t l_key = (l_w << 8) | l_src[i];
			auto l_it = l_dict.find(l_key);
			if (l_it != l_dict.end()) {
				l_w = l_it->second;
				continue;
			}
			l_bw.put(l_w, code_bits(l_next));
			if (l_next < DICT_MAX) {
				l_dict[l_key] = l_next++;
			} else {
				l_bw.put(CLEAR, code_bits(l_next));
				l_dict.clear();
				l_next = FIRST_FREE;
			}
			l_w = l_src[i];
		}
		l_bw.put(l_w, code_bits(l_next));
		l_bw.flush();
	}

	return data(l_out.data(), l_out.size());
}

data lzw::decompress(const data& a_in)
{
	const std::vector<uint8_t>& l_in = a_in.bytes();
	if (l_in.size() < HEADER_SIZE)
		throw std::runtime_error("lzw: truncated header");

	uint64_t l_size = 0;
	for (size_t i = 0; i < HEADER_SIZE; i++)
		l_size = (l_size << 8) | l_in[i];

	std::vector<uint8_t> l_out;
	bit_reader l_br(l_in.data() + HEADER_SIZE, l_in.size() - HEADER_SIZE);

	// dictionary entries from FIRST_FREE up: a prefix code and a last byte
	std::vector<uint32_t> l_prefix(DICT_MAX);
	std::vector<uint8_t> l_last(DICT_MAX);
	uint32_t l_next = FIRST_FREE;
	bool l_have_prev = false;
	uint32_t l_prev = 0;
	std::vector<uint8_t> l_str;

	while (l_out.size() < l_size) {
		// the encoder is one entry ahead of the decoder once a code has been read
		uint32_t l_count = std::min<uint32_t>(l_next + (l_have_prev ? 1 : 0), DICT_MAX);
		uint32_t l_code = l_br.get(code_bits(l_count));

		if (l_code == CLEAR) {
			l_next = FIRST_FREE;
			l_have_prev = false;
			continue;
		}

		l_str.clear();
		if (l_code < 256) {
			l_str.push_back(static_cast<uint8_t>(l_code));
		} else if (l_code < l_next || (l_have_prev && l_code == l_next && l_next < DICT_MAX)) {
			if (l_code == l_next) {
				// the string being defined by this very code: previous string plus its own first byte
				uint32_t l_c = l_prev;
				for (; l_c >= FIRST_FREE; l_c = l_prefix[l_c])
					l_str.push_back(l_last[l_c]);
				l_str.push_back(static_cast<uint8_t>(l_c));
				std::reverse(l_str.begin(), l_str.end());
				l_str.push_back(l_str[0]);
			} else {
				uint32_t l_c = l_code;
				for (; l_c >= FIRST_FREE; l_c = l_prefix[l_c])
					l_str.push_back(l_last[l_c]);
				l_str.push_back(static_cast<uint8_t>(l_c));
				std::reverse(l_str.begin(), l_str.end());
			}
		} else {
			throw std::runtime_error("lzw: invalid code");
		}

		if (l_str.size() > l_size - l_out.size())
			throw std::runtime_error("lzw: data runs past the declared size");

		if (l_have_prev && l_next < DICT_MAX) {
			l_prefix[l_next] = l_prev;
			l_last[l_next] = l_str[0];
			l_next++;
		}
		l_out.insert(l_out.end(), l_str.begin(), l_str.end());
		l_prev = l_code;
		l_have_prev = true;
	}

	if (!l_br.done())
		throw std::runtime_error("lzw: trailing data");
	return data(l_out.data(), l_out.size());
}
