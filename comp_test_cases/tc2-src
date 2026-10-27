#include "lzma.h"

#include <algorithm>
#include <cstdint>
#include <vector>

namespace {

typedef uint16_t prob;

const unsigned PROB_BITS = 11;
const prob PROB_INIT = 1u << (PROB_BITS - 1);
const unsigned MOVE_BITS = 5;
const uint32_t TOP = 1u << 24;

const unsigned NUM_STATES = 12;
const unsigned LIT_STATES = 7;	// states below this follow a literal
const unsigned MATCH_MIN = 2;
const unsigned MATCH_MAX = 273;
const unsigned END_POS_MODEL_INDEX = 14;
const unsigned NUM_FULL_DISTANCES = 1u << (END_POS_MODEL_INDEX / 2);
const unsigned ALIGN_BITS = 4;
const uint32_t END_MARKER = 0xFFFFFFFFu;

const size_t HEADER_SIZE = 13;
const uint32_t DICT_MIN = 1u << 12;
const uint32_t DICT_MAX = 1u << 24;

// properties used by the encoder
const unsigned LC = 3;
const unsigned LP = 0;
const unsigned PB = 2;

inline void reset(prob& a_p)
{
	a_p = PROB_INIT;
}

template <typename T>
void reset(T& a_array)
{
	for (auto& l_e : a_array)
		reset(l_e);
}

struct len_model {
	prob choice;
	prob choice2;
	prob low[16][8];
	prob mid[16][8];
	prob high[256];

	len_model()
	{
		reset(choice);
		reset(choice2);
		reset(low);
		reset(mid);
		reset(high);
	}
};

// every adaptive probability the coder uses; encoder and decoder must evolve it identically
struct model {
	prob is_match[NUM_STATES][16];
	prob is_rep[NUM_STATES];
	prob is_rep_g0[NUM_STATES];
	prob is_rep_g1[NUM_STATES];
	prob is_rep_g2[NUM_STATES];
	prob is_rep0_long[NUM_STATES][16];
	prob pos_slot[4][64];
	prob pos_special[1 + NUM_FULL_DISTANCES - END_POS_MODEL_INDEX];
	prob align[1 << ALIGN_BITS];
	len_model len;
	len_model rep_len;
	std::vector<prob> literal;

	model(unsigned a_lc, unsigned a_lp)
		: literal(static_cast<size_t>(0x300) << (a_lc + a_lp), PROB_INIT)
	{
		reset(is_match);
		reset(is_rep);
		reset(is_rep_g0);
		reset(is_rep_g1);
		reset(is_rep_g2);
		reset(is_rep0_long);
		reset(pos_slot);
		reset(pos_special);
		reset(align);
	}
};

unsigned state_after_literal(unsigned a_state)
{
	return a_state < 4 ? 0 : (a_state < 10 ? a_state - 3 : a_state - 6);
}

unsigned state_after_match(unsigned a_state)
{
	return a_state < LIT_STATES ? 7 : 10;
}

unsigned state_after_rep(unsigned a_state)
{
	return a_state < LIT_STATES ? 8 : 11;
}

unsigned state_after_short_rep(unsigned a_state)
{
	return a_state < LIT_STATES ? 9 : 11;
}

// ---- range decoder ----

class range_decoder {
public:
	range_decoder(const uint8_t *a_p, size_t a_n)
		: m_p(a_p), m_end(a_p + a_n)
	{
		if (a_n < 5 || a_p[0] != 0)
			throw std::runtime_error("lzma: bad range coder start");
		for (int i = 1; i < 5; i++)
			m_code = (m_code << 8) | a_p[i];
		m_p += 5;
		if (m_code == m_range)
			throw std::runtime_error("lzma: bad range coder start");
	}

	unsigned bit(prob& a_p)
	{
		uint32_t l_bound = (m_range >> PROB_BITS) * a_p;
		unsigned l_bit;
		if (m_code < l_bound) {
			m_range = l_bound;
			a_p += ((1u << PROB_BITS) - a_p) >> MOVE_BITS;
			l_bit = 0;
		} else {
			m_range -= l_bound;
			m_code -= l_bound;
			a_p -= a_p >> MOVE_BITS;
			l_bit = 1;
		}
		normalize();
		return l_bit;
	}

	uint32_t direct(unsigned a_bits)
	{
		uint32_t l_res = 0;
		while (a_bits--) {
			m_range >>= 1;
			m_code -= m_range;
			uint32_t l_t = 0 - (m_code >> 31);
			m_code += m_range & l_t;
			if (m_code == m_range)
				throw std::runtime_error("lzma: corrupt data");
			normalize();
			l_res = (l_res << 1) + l_t + 1;
		}
		return l_res;
	}

private:
	void normalize()
	{
		if (m_range < TOP) {
			if (m_p == m_end)
				throw std::runtime_error("lzma: truncated data");
			m_range <<= 8;
			m_code = (m_code << 8) | *m_p++;
		}
	}

	const uint8_t *m_p;
	const uint8_t *m_end;
	uint32_t m_range = 0xFFFFFFFFu;
	uint32_t m_code = 0;
};

// probs[1 .. (1 << a_bits) - 1] form a binary tree, most significant bit first
unsigned tree_decode(range_decoder& a_rc, prob *a_probs, unsigned a_bits)
{
	unsigned l_m = 1;
	for (unsigned i = 0; i < a_bits; i++)
		l_m = (l_m << 1) | a_rc.bit(a_probs[l_m]);
	return l_m - (1u << a_bits);
}

unsigned tree_decode_reverse(range_decoder& a_rc, prob *a_probs, unsigned a_bits)
{
	unsigned l_m = 1;
	unsigned l_sym = 0;
	for (unsigned i = 0; i < a_bits; i++) {
		unsigned l_bit = a_rc.bit(a_probs[l_m]);
		l_m = (l_m << 1) | l_bit;
		l_sym |= l_bit << i;
	}
	return l_sym;
}

class decoder {
public:
	decoder(unsigned a_lc, unsigned a_lp, unsigned a_pb, uint32_t a_dict, const uint8_t *a_p, size_t a_n)
		: m_lc(a_lc), m_lp(a_lp), m_pb(a_pb), m_dict(a_dict), m_model(a_lc, a_lp), m_rc(a_p, a_n)
	{
	}

	// a_known: stop after a_size bytes; otherwise run until the end marker
	std::vector<uint8_t> run(bool a_known, uint64_t a_size)
	{
		std::vector<uint8_t> l_out;
		unsigned l_state = 0;
		uint32_t l_rep0 = 0, l_rep1 = 0, l_rep2 = 0, l_rep3 = 0;
		const unsigned l_pb_mask = (1u << m_pb) - 1;
		const unsigned l_lp_mask = (1u << m_lp) - 1;

		for (;;) {
			if (a_known && l_out.size() == a_size)
				break;

			unsigned l_pos_state = l_out.size() & l_pb_mask;
			if (!m_rc.bit(m_model.is_match[l_state][l_pos_state])) {
				unsigned l_prev = l_out.empty() ? 0 : l_out.back();
				size_t l_lit_state = ((l_out.size() & l_lp_mask) << m_lc) + (l_prev >> (8 - m_lc));
				prob *l_probs = &m_model.literal[0x300 * l_lit_state];
				unsigned l_sym = 1;
				if (l_state >= LIT_STATES) {
					if (l_rep0 >= l_out.size())
						throw std::runtime_error("lzma: corrupt data");
					unsigned l_match_byte = l_out[l_out.size() - l_rep0 - 1];
					do {
						unsigned l_match_bit = (l_match_byte >> 7) & 1;
						l_match_byte <<= 1;
						unsigned l_bit = m_rc.bit(l_probs[((1 + l_match_bit) << 8) + l_sym]);
						l_sym = (l_sym << 1) | l_bit;
						if (l_match_bit != l_bit)
							break;
					} while (l_sym < 0x100);
				}
				while (l_sym < 0x100)
					l_sym = (l_sym << 1) | m_rc.bit(l_probs[l_sym]);
				l_out.push_back(static_cast<uint8_t>(l_sym));
				l_state = state_after_literal(l_state);
				continue;
			}

			unsigned l_len;
			if (m_rc.bit(m_model.is_rep[l_state])) {
				if (l_out.empty())
					throw std::runtime_error("lzma: corrupt data");
				if (!m_rc.bit(m_model.is_rep_g0[l_state])) {
					if (!m_rc.bit(m_model.is_rep0_long[l_state][l_pos_state])) {
						if (l_rep0 >= l_out.size())
							throw std::runtime_error("lzma: corrupt data");
						l_state = state_after_short_rep(l_state);
						l_out.push_back(l_out[l_out.size() - l_rep0 - 1]);
						continue;
					}
				} else {
					uint32_t l_dist;
					if (!m_rc.bit(m_model.is_rep_g1[l_state])) {
						l_dist = l_rep1;
					} else {
						if (!m_rc.bit(m_model.is_rep_g2[l_state])) {
							l_dist = l_rep2;
						} else {
							l_dist = l_rep3;
							l_rep3 = l_rep2;
						}
						l_rep2 = l_rep1;
					}
					l_rep1 = l_rep0;
					l_rep0 = l_dist;
				}
				l_len = decode_len(m_model.rep_len, l_pos_state);
				l_state = state_after_rep(l_state);
			} else {
				l_rep3 = l_rep2;
				l_rep2 = l_rep1;
				l_rep1 = l_rep0;
				l_len = decode_len(m_model.len, l_pos_state);
				l_state = state_after_match(l_state);
				l_rep0 = decode_distance(l_len);
				if (l_rep0 == END_MARKER) {
					if (a_known && l_out.size() != a_size)
						throw std::runtime_error("lzma: stream ended before the declared size");
					break;
				}
			}

			l_len += MATCH_MIN;
			if (l_rep0 >= m_dict || l_rep0 >= l_out.size())
				throw std::runtime_error("lzma: match distance out of range");
			if (a_known && l_len > a_size - l_out.size())
				throw std::runtime_error("lzma: match runs past the declared size");
			for (unsigned i = 0; i < l_len; i++)
				l_out.push_back(l_out[l_out.size() - l_rep0 - 1]);
		}
		return l_out;
	}

private:
	// returns the match length minus MATCH_MIN
	unsigned decode_len(len_model& a_lm, unsigned a_pos_state)
	{
		if (!m_rc.bit(a_lm.choice))
			return tree_decode(m_rc, a_lm.low[a_pos_state], 3);
		if (!m_rc.bit(a_lm.choice2))
			return 8 + tree_decode(m_rc, a_lm.mid[a_pos_state], 3);
		return 16 + tree_decode(m_rc, a_lm.high, 8);
	}

	// a_len is the match length minus MATCH_MIN; returns the distance minus 1
	uint32_t decode_distance(unsigned a_len)
	{
		unsigned l_len_state = std::min(a_len, 3u);
		unsigned l_slot = tree_decode(m_rc, m_model.pos_slot[l_len_state], 6);
		if (l_slot < 4)
			return l_slot;

		unsigned l_footer = (l_slot >> 1) - 1;
		uint32_t l_dist = (2 | (l_slot & 1)) << l_footer;
		if (l_slot < END_POS_MODEL_INDEX) {
			l_dist += tree_decode_reverse(m_rc, &m_model.pos_special[l_dist - l_slot], l_footer);
		} else {
			l_dist += m_rc.direct(l_footer - ALIGN_BITS) << ALIGN_BITS;
			l_dist += tree_decode_reverse(m_rc, m_model.align, ALIGN_BITS);
		}
		return l_dist;
	}

	unsigned m_lc, m_lp, m_pb;
	uint32_t m_dict;
	model m_model;
	range_decoder m_rc;
};

// ---- range encoder ----

class range_encoder {
public:
	explicit range_encoder(std::vector<uint8_t>& a_out)
		: m_out(a_out)
	{
	}

	void bit(prob& a_p, unsigned a_bit)
	{
		uint32_t l_bound = (m_range >> PROB_BITS) * a_p;
		if (!a_bit) {
			m_range = l_bound;
			a_p += ((1u << PROB_BITS) - a_p) >> MOVE_BITS;
		} else {
			m_low += l_bound;
			m_range -= l_bound;
			a_p -= a_p >> MOVE_BITS;
		}
		while (m_range < TOP) {
			m_range <<= 8;
			shift_low();
		}
	}

	void direct(uint32_t a_val, unsigned a_bits)
	{
		while (a_bits--) {
			m_range >>= 1;
			if ((a_val >> a_bits) & 1)
				m_low += m_range;
			while (m_range < TOP) {
				m_range <<= 8;
				shift_low();
			}
		}
	}

	void flush()
	{
		for (int i = 0; i < 5; i++)
			shift_low();
	}

private:
	void shift_low()
	{
		if (static_cast<uint32_t>(m_low) < 0xFF000000u || (m_low >> 32) != 0) {
			uint8_t l_temp = m_cache;
			do {
				m_out.push_back(static_cast<uint8_t>(l_temp + (m_low >> 32)));
				l_temp = 0xFF;
			} while (--m_cache_size);
			m_cache = static_cast<uint8_t>(m_low >> 24);
		}
		m_cache_size++;
		m_low = (m_low & 0x00FFFFFFu) << 8;
	}

	std::vector<uint8_t>& m_out;
	uint64_t m_low = 0;
	uint32_t m_range = 0xFFFFFFFFu;
	uint8_t m_cache = 0;
	uint64_t m_cache_size = 1;
};

void tree_encode(range_encoder& a_rc, prob *a_probs, unsigned a_bits, unsigned a_sym)
{
	unsigned l_m = 1;
	for (unsigned i = a_bits; i-- > 0; ) {
		unsigned l_bit = (a_sym >> i) & 1;
		a_rc.bit(a_probs[l_m], l_bit);
		l_m = (l_m << 1) | l_bit;
	}
}

void tree_encode_reverse(range_encoder& a_rc, prob *a_probs, unsigned a_bits, unsigned a_sym)
{
	unsigned l_m = 1;
	for (unsigned i = 0; i < a_bits; i++) {
		unsigned l_bit = a_sym & 1;
		a_sym >>= 1;
		a_rc.bit(a_probs[l_m], l_bit);
		l_m = (l_m << 1) | l_bit;
	}
}

// ---- match finder ----

// hash chains over 3 byte prefixes; positions are stored +1 so 0 means none
class match_finder {
public:
	match_finder(const uint8_t *a_d, size_t a_n, uint32_t a_dict)
		: m_d(a_d), m_n(a_n), m_dict(a_dict), m_head(HASH_SIZE, 0), m_prev(a_n, 0)
	{
	}

	// longest match for the bytes at a_i, at least 3 long and at most a_max, from earlier positions
	// within the dictionary. Returns 0 if there is none, else the length and sets a_dist (1 = previous byte).
	size_t find(size_t a_i, size_t a_max, size_t& a_dist) const
	{
		if (a_i + 3 > m_n)
			return 0;

		size_t l_best = 0;
		uint32_t l_cand = m_head[hash(a_i)];
		for (unsigned l_chain = 0; l_cand && l_chain < MAX_CHAIN; l_chain++) {
			size_t l_p = l_cand - 1;
			size_t l_dist = a_i - l_p;
			if (l_dist > m_dict)
				break;
			if (m_d[l_p + l_best] == m_d[a_i + l_best]) {
				size_t l_len = match_len(m_d, l_p, a_i, a_max);
				if (l_len > l_best) {
					l_best = l_len;
					a_dist = l_dist;
					if (l_best >= a_max)
						break;
				}
			}
			l_cand = m_prev[l_p];
		}
		return l_best >= 3 ? l_best : 0;
	}

	void insert(size_t a_i)
	{
		if (a_i + 3 > m_n)
			return;
		uint32_t l_h = hash(a_i);
		m_prev[a_i] = m_head[l_h];
		m_head[l_h] = static_cast<uint32_t>(a_i + 1);
	}

	static size_t match_len(const uint8_t *a_d, size_t a_a, size_t a_b, size_t a_max)
	{
		size_t l_len = 0;
		while (l_len < a_max && a_d[a_a + l_len] == a_d[a_b + l_len])
			l_len++;
		return l_len;
	}

private:
	static const size_t HASH_SIZE = 1u << 16;
	static const unsigned MAX_CHAIN = 48;

	uint32_t hash(size_t a_i) const
	{
		uint32_t l_v = (static_cast<uint32_t>(m_d[a_i]) << 16) | (static_cast<uint32_t>(m_d[a_i + 1]) << 8) | m_d[a_i + 2];
		return (l_v * 2654435761u) >> 16;
	}

	const uint8_t *m_d;
	size_t m_n;
	uint32_t m_dict;
	std::vector<uint32_t> m_head;
	std::vector<uint32_t> m_prev;
};

class encoder {
public:
	encoder(const uint8_t *a_d, size_t a_n, uint32_t a_dict, std::vector<uint8_t>& a_out)
		: m_d(a_d), m_n(a_n), m_finder(a_d, a_n, a_dict), m_model(LC, LP), m_rc(a_out)
	{
	}

	// greedy parse: repeat matches first, then the longest dictionary match, then a short rep, else a literal
	void run()
	{
		unsigned l_state = 0;
		uint32_t l_reps[4] = { 0, 0, 0, 0 };	// distance minus 1
		const unsigned l_pb_mask = (1u << PB) - 1;

		size_t l_i = 0;
		while (l_i < m_n) {
			size_t l_max = std::min<size_t>(m_n - l_i, MATCH_MAX);
			unsigned l_pos_state = l_i & l_pb_mask;

			size_t l_rep_len = 0;
			unsigned l_rep_idx = 0;
			for (unsigned r = 0; r < 4; r++) {
				if (l_reps[r] < l_i) {
					size_t l_len = match_finder::match_len(m_d, l_i - l_reps[r] - 1, l_i, l_max);
					if (l_len > l_rep_len) {
						l_rep_len = l_len;
						l_rep_idx = r;
					}
				}
			}

			size_t l_dist = 0;
			size_t l_main_len = m_finder.find(l_i, l_max, l_dist);
			m_finder.insert(l_i);
			// a 3 byte match from far away costs more than the literals it replaces
			if (l_main_len == 3 && l_dist > (1u << 12))
				l_main_len = 0;

			size_t l_len;
			if (l_rep_len >= 2 && l_rep_len + 1 >= l_main_len) {
				l_len = l_rep_len;
				m_rc.bit(m_model.is_match[l_state][l_pos_state], 1);
				m_rc.bit(m_model.is_rep[l_state], 1);
				if (l_rep_idx == 0) {
					m_rc.bit(m_model.is_rep_g0[l_state], 0);
					m_rc.bit(m_model.is_rep0_long[l_state][l_pos_state], 1);
				} else {
					m_rc.bit(m_model.is_rep_g0[l_state], 1);
					if (l_rep_idx == 1) {
						m_rc.bit(m_model.is_rep_g1[l_state], 0);
					} else {
						m_rc.bit(m_model.is_rep_g1[l_state], 1);
						m_rc.bit(m_model.is_rep_g2[l_state], l_rep_idx - 2);
					}
					uint32_t l_d = l_reps[l_rep_idx];
					for (unsigned k = l_rep_idx; k > 0; k--)
						l_reps[k] = l_reps[k - 1];
					l_reps[0] = l_d;
				}
				encode_len(m_model.rep_len, l_len, l_pos_state);
				l_state = state_after_rep(l_state);
			} else if (l_main_len >= 3) {
				l_len = l_main_len;
				m_rc.bit(m_model.is_match[l_state][l_pos_state], 1);
				m_rc.bit(m_model.is_rep[l_state], 0);
				encode_len(m_model.len, l_len, l_pos_state);
				encode_distance(static_cast<uint32_t>(l_dist - 1), l_len);
				l_reps[3] = l_reps[2];
				l_reps[2] = l_reps[1];
				l_reps[1] = l_reps[0];
				l_reps[0] = static_cast<uint32_t>(l_dist - 1);
				l_state = state_after_match(l_state);
			} else if (l_reps[0] < l_i && m_d[l_i] == m_d[l_i - l_reps[0] - 1]) {
				l_len = 1;
				m_rc.bit(m_model.is_match[l_state][l_pos_state], 1);
				m_rc.bit(m_model.is_rep[l_state], 1);
				m_rc.bit(m_model.is_rep_g0[l_state], 0);
				m_rc.bit(m_model.is_rep0_long[l_state][l_pos_state], 0);
				l_state = state_after_short_rep(l_state);
			} else {
				l_len = 1;
				m_rc.bit(m_model.is_match[l_state][l_pos_state], 0);
				encode_literal(l_i, l_state, l_reps[0]);
				l_state = state_after_literal(l_state);
			}

			for (size_t j = l_i + 1; j < l_i + l_len; j++)
				m_finder.insert(j);
			l_i += l_len;
		}
		m_rc.flush();
	}

private:
	void encode_literal(size_t a_i, unsigned a_state, uint32_t a_rep0)
	{
		unsigned l_prev = a_i ? m_d[a_i - 1] : 0;
		size_t l_lit_state = ((a_i & ((1u << LP) - 1)) << LC) + (l_prev >> (8 - LC));
		prob *l_probs = &m_model.literal[0x300 * l_lit_state];
		unsigned l_byte = m_d[a_i];
		unsigned l_m = 1;
		int l_bit_idx = 7;

		if (a_state >= LIT_STATES) {
			// while the bits agree with the byte at the last match distance, use it as context
			unsigned l_match_byte = m_d[a_i - a_rep0 - 1];
			for (; l_bit_idx >= 0; l_bit_idx--) {
				unsigned l_match_bit = (l_match_byte >> l_bit_idx) & 1;
				unsigned l_bit = (l_byte >> l_bit_idx) & 1;
				m_rc.bit(l_probs[((1 + l_match_bit) << 8) + l_m], l_bit);
				l_m = (l_m << 1) | l_bit;
				if (l_match_bit != l_bit) {
					l_bit_idx--;
					break;
				}
			}
		}
		for (; l_bit_idx >= 0; l_bit_idx--) {
			unsigned l_bit = (l_byte >> l_bit_idx) & 1;
			m_rc.bit(l_probs[l_m], l_bit);
			l_m = (l_m << 1) | l_bit;
		}
	}

	void encode_len(len_model& a_lm, size_t a_len, unsigned a_pos_state)
	{
		unsigned l_len = static_cast<unsigned>(a_len - MATCH_MIN);
		if (l_len < 8) {
			m_rc.bit(a_lm.choice, 0);
			tree_encode(m_rc, a_lm.low[a_pos_state], 3, l_len);
		} else if (l_len < 16) {
			m_rc.bit(a_lm.choice, 1);
			m_rc.bit(a_lm.choice2, 0);
			tree_encode(m_rc, a_lm.mid[a_pos_state], 3, l_len - 8);
		} else {
			m_rc.bit(a_lm.choice, 1);
			m_rc.bit(a_lm.choice2, 1);
			tree_encode(m_rc, a_lm.high, 8, l_len - 16);
		}
	}

	// a_dist is the distance minus 1
	void encode_distance(uint32_t a_dist, size_t a_len)
	{
		unsigned l_len_state = static_cast<unsigned>(std::min<size_t>(a_len - MATCH_MIN, 3));
		unsigned l_slot;
		if (a_dist < 4) {
			l_slot = a_dist;
		} else {
			unsigned l_top = 0;	// index of the highest set bit
			while ((a_dist >> (l_top + 1)) != 0)
				l_top++;
			l_slot = (l_top << 1) | ((a_dist >> (l_top - 1)) & 1);
		}
		tree_encode(m_rc, m_model.pos_slot[l_len_state], 6, l_slot);
		if (l_slot < 4)
			return;

		unsigned l_footer = (l_slot >> 1) - 1;
		uint32_t l_base = (2 | (l_slot & 1)) << l_footer;
		uint32_t l_reduced = a_dist - l_base;
		if (l_slot < END_POS_MODEL_INDEX) {
			tree_encode_reverse(m_rc, &m_model.pos_special[l_base - l_slot], l_footer, l_reduced);
		} else {
			m_rc.direct(l_reduced >> ALIGN_BITS, l_footer - ALIGN_BITS);
			tree_encode_reverse(m_rc, m_model.align, ALIGN_BITS, l_reduced & ((1u << ALIGN_BITS) - 1));
		}
	}

	const uint8_t *m_d;
	size_t m_n;
	match_finder m_finder;
	model m_model;
	range_encoder m_rc;
};

uint64_t get_le(const uint8_t *a_p, unsigned a_bytes)
{
	uint64_t l_v = 0;
	for (unsigned i = a_bytes; i-- > 0; )
		l_v = (l_v << 8) | a_p[i];
	return l_v;
}

void put_le(std::vector<uint8_t>& a_out, uint64_t a_val, unsigned a_bytes)
{
	for (unsigned i = 0; i < a_bytes; i++)
		a_out.push_back(static_cast<uint8_t>(a_val >> (i * 8)));
}

} // namespace

data lzma::compress(const data& a_in)
{
	const std::vector<uint8_t>& l_src = a_in.bytes();

	// a dictionary as large as the input is enough; the stream is never read at a longer distance.
	// Rounded up to a power of two, as the xz tools only accept sizes of the form 2^n or 2^n + 2^(n-1).
	uint32_t l_dict = DICT_MIN;
	while (l_dict < DICT_MAX && l_dict < l_src.size())
		l_dict <<= 1;

	std::vector<uint8_t> l_out;
	l_out.push_back(static_cast<uint8_t>((PB * 5 + LP) * 9 + LC));
	put_le(l_out, l_dict, 4);
	put_le(l_out, l_src.size(), 8);

	encoder l_enc(l_src.data(), l_src.size(), l_dict, l_out);
	l_enc.run();

	return data(l_out.data(), l_out.size());
}

data lzma::decompress(const data& a_in)
{
	const std::vector<uint8_t>& l_in = a_in.bytes();
	if (l_in.size() < HEADER_SIZE)
		throw std::runtime_error("lzma: truncated header");

	unsigned l_props = l_in[0];
	if (l_props >= 9 * 5 * 5)
		throw std::runtime_error("lzma: invalid properties");
	unsigned l_lc = l_props % 9;
	l_props /= 9;
	unsigned l_lp = l_props % 5;
	unsigned l_pb = l_props / 5;

	uint32_t l_dict = static_cast<uint32_t>(get_le(&l_in[1], 4));
	if (l_dict < DICT_MIN)
		l_dict = DICT_MIN;
	uint64_t l_size = get_le(&l_in[5], 8);
	bool l_known = l_size != UINT64_MAX;

	decoder l_dec(l_lc, l_lp, l_pb, l_dict, l_in.data() + HEADER_SIZE, l_in.size() - HEADER_SIZE);
	std::vector<uint8_t> l_out = l_dec.run(l_known, l_size);
	return data(l_out.data(), l_out.size());
}
