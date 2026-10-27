#include "huffman.h"

#include <cstdint>
#include <functional>
#include <queue>
#include <utility>
#include <vector>

namespace {

struct node {
	uint64_t freq;
	int left;	// child indices, -1 for a leaf
	int right;
	int sym;	// byte value, for leaves
};

// Builds the code tree from byte frequencies and returns the root index.
// Ties are broken by creation order (leaves in byte order first), so the same
// frequencies always give the same tree. Needs at least two nonzero frequencies.
int build_tree(const uint64_t *a_freq, std::vector<node>& a_nodes)
{
	typedef std::pair<uint64_t, int> entry;	// frequency, node index
	std::priority_queue<entry, std::vector<entry>, std::greater<entry> > l_queue;

	for (int l_sym = 0; l_sym < 256; l_sym++) {
		if (a_freq[l_sym]) {
			a_nodes.push_back({ a_freq[l_sym], -1, -1, l_sym });
			l_queue.push(entry(a_freq[l_sym], static_cast<int>(a_nodes.size()) - 1));
		}
	}

	while (l_queue.size() > 1) {
		entry l_a = l_queue.top();
		l_queue.pop();
		entry l_b = l_queue.top();
		l_queue.pop();
		a_nodes.push_back({ l_a.first + l_b.first, l_a.second, l_b.second, -1 });
		l_queue.push(entry(a_nodes.back().freq, static_cast<int>(a_nodes.size()) - 1));
	}
	return l_queue.top().second;
}

// fills a_codes[symbol] with the path of 0/1 branches from the root to each leaf
void assign_codes(const std::vector<node>& a_nodes, int a_index, std::vector<uint8_t>& a_path, std::vector<uint8_t> *a_codes)
{
	const node& l_n = a_nodes[a_index];
	if (l_n.left < 0) {
		a_codes[l_n.sym] = a_path;
		return;
	}
	a_path.push_back(0);
	assign_codes(a_nodes, l_n.left, a_path, a_codes);
	a_path.back() = 1;
	assign_codes(a_nodes, l_n.right, a_path, a_codes);
	a_path.pop_back();
}

} // namespace

data huffman::compress(const data& a_in)
{
	const std::vector<uint8_t>& l_src = a_in.bytes();

	data l_out;
	l_out.set_byte_order(data::NETWORK_ORDER);
	l_out.append_uint64(l_src.size());
	if (l_src.empty())
		return l_out;

	uint64_t l_freq[256] = { 0 };
	for (uint8_t l_byte : l_src)
		l_freq[l_byte]++;

	unsigned l_distinct = 0;
	uint64_t l_max = 0;
	for (int l_sym = 0; l_sym < 256; l_sym++) {
		if (l_freq[l_sym]) {
			l_distinct++;
			if (l_freq[l_sym] > l_max)
				l_max = l_freq[l_sym];
		}
	}

	unsigned l_width = 1;
	while (l_width < 8 && (l_max >> (l_width * 8)) != 0)
		l_width++;

	l_out.append_uint8(static_cast<uint8_t>(l_width));
	l_out.append_uint16(static_cast<uint16_t>(l_distinct));
	for (int l_sym = 0; l_sym < 256; l_sym++) {
		if (l_freq[l_sym]) {
			l_out.append_uint8(static_cast<uint8_t>(l_sym));
			l_out.append_arbitrary(l_freq[l_sym], l_width * 8);
		}
	}

	// a single repeated value is fully described by the header
	if (l_distinct == 1)
		return l_out;

	std::vector<node> l_nodes;
	int l_root = build_tree(l_freq, l_nodes);
	std::vector<uint8_t> l_codes[256];
	std::vector<uint8_t> l_path;
	assign_codes(l_nodes, l_root, l_path, l_codes);

	uint8_t l_acc = 0;
	int l_bits = 0;
	for (uint8_t l_byte : l_src) {
		for (uint8_t l_bit : l_codes[l_byte]) {
			l_acc = static_cast<uint8_t>((l_acc << 1) | l_bit);
			if (++l_bits == 8) {
				l_out.append_uint8(l_acc);
				l_acc = 0;
				l_bits = 0;
			}
		}
	}
	if (l_bits)
		l_out.append_uint8(static_cast<uint8_t>(l_acc << (8 - l_bits)));

	return l_out;
}

data huffman::decompress(const data& a_in)
{
	data l_in(a_in);
	l_in.set_byte_order(data::NETWORK_ORDER);
	l_in.set_read_cursor(0);

	uint64_t l_size;
	uint64_t l_freq[256] = { 0 };
	unsigned l_distinct = 0;
	try {
		l_size = l_in.read_uint64();
		if (l_size == 0) {
			if (l_in.get_read_cursor() != l_in.size())
				throw std::runtime_error("huffman: unexpected data after empty header");
			return data();
		}

		unsigned l_width = l_in.read_uint8();
		if (l_width < 1 || l_width > 8)
			throw std::runtime_error("huffman: invalid frequency width");
		l_distinct = l_in.read_uint16();
		if (l_distinct < 1 || l_distinct > 256)
			throw std::runtime_error("huffman: invalid symbol count");

		uint64_t l_total = 0;
		for (unsigned i = 0; i < l_distinct; i++) {
			uint8_t l_sym = l_in.read_uint8();
			uint64_t l_f = l_in.read_arbitrary(l_width * 8);
			if (l_freq[l_sym] != 0 || l_f == 0 || l_f > UINT64_MAX - l_total)
				throw std::runtime_error("huffman: invalid frequency table");
			l_freq[l_sym] = l_f;
			l_total += l_f;
		}
		if (l_total != l_size)
			throw std::runtime_error("huffman: frequency table does not match original size");
	} catch (const std::out_of_range&) {
		throw std::runtime_error("huffman: truncated header");
	}

	data l_out;
	l_out.set_byte_order(data::NETWORK_ORDER);

	if (l_distinct == 1) {
		uint8_t l_sym = 0;
		while (l_freq[l_sym] == 0)
			l_sym++;
		for (uint64_t i = 0; i < l_size; i++)
			l_out.append_uint8(l_sym);
		return l_out;
	}

	std::vector<node> l_nodes;
	int l_root = build_tree(l_freq, l_nodes);

	const std::vector<uint8_t>& l_bytes = l_in.bytes();
	size_t l_pos = l_in.get_read_cursor();
	int l_bit = 7;
	int l_cur = l_root;
	while (l_out.size() < l_size) {
		if (l_pos >= l_bytes.size())
			throw std::runtime_error("huffman: truncated code data");
		int l_branch = (l_bytes[l_pos] >> l_bit) & 1;
		if (--l_bit < 0) {
			l_bit = 7;
			l_pos++;
		}
		l_cur = l_branch ? l_nodes[l_cur].right : l_nodes[l_cur].left;
		if (l_nodes[l_cur].left < 0) {
			l_out.append_uint8(static_cast<uint8_t>(l_nodes[l_cur].sym));
			l_cur = l_root;
		}
	}
	return l_out;
}
