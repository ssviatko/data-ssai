#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include "data.h"
#include "huffman.h"
#include "lzma.h"

static int g_failures = 0;

static void check(bool a_cond, const char *a_desc)
{
	std::cout << (a_cond ? "PASS: " : "FAIL: ") << a_desc << std::endl;
	if (!a_cond)
		g_failures++;
}

static void run_tests()
{
	const uint8_t l_src[] = { 0x01, 0x02, 0x03, 0x04 };
	const uint8_t l_more[] = { 0xaa, 0xbb };

	// default constructor
	data l_empty;
	check(l_empty.size() == 0, "default constructor creates empty data");

	// byte-copy constructor
	data l_d(l_src, sizeof(l_src));
	check(l_d.size() == 4, "constructor sets size");
	check(l_d.bytes()[0] == 0x01 && l_d.bytes()[3] == 0x04, "constructor copies bytes");

	// string constructor
	data l_str(std::string("a\0b", 3));
	check(l_str.size() == 3 && l_str.bytes()[0] == 'a' && l_str.bytes()[1] == 0 && l_str.bytes()[2] == 'b', "string constructor copies raw bytes");
	check(l_str.get_write_cursor() == 3 && l_str.get_read_cursor() == 0, "string constructor sets cursors");
	check(data(std::string()).size() == 0, "string constructor accepts empty string");

	// append
	l_d.append(l_more, sizeof(l_more));
	check(l_d.size() == 6, "append grows size");
	check(l_d.bytes()[4] == 0xaa && l_d.bytes()[5] == 0xbb, "append adds bytes at end");

	// size
	check(l_d.size() == l_d.bytes().size(), "size matches underlying vector");

	// bytes
	check(l_d.bytes().front() == 0x01 && l_d.bytes().back() == 0xbb, "bytes exposes contents");

	// clear
	l_d.clear();
	check(l_d.size() == 0 && l_d.bytes().empty(), "clear empties data");

	// set_byte_order / get_byte_order
	check(l_d.get_byte_order() == data::NETWORK_ORDER, "default byte order is network");
	l_d.set_byte_order(data::HOST_ORDER);
	check(l_d.get_byte_order() == data::HOST_ORDER, "set_byte_order selects host order");
	l_d.set_byte_order(data::NETWORK_ORDER);
	check(l_d.get_byte_order() == data::NETWORK_ORDER, "set_byte_order selects network order");

	// integer appends, network order (big endian)
	l_d.append_uint8(0x7f);
	l_d.append_uint16(0x1234);
	l_d.append_uint32(0x01020304);
	l_d.append_uint64(0x0102030405060708ULL);
	const uint8_t l_net[] = {
		0x7f,
		0x12, 0x34,
		0x01, 0x02, 0x03, 0x04,
		0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08
	};
	check(l_d.size() == sizeof(l_net), "network order integer appends have correct total size");
	check(l_d.bytes() == std::vector<uint8_t>(l_net, l_net + sizeof(l_net)),
	    "network order integers are big endian");

	// integer appends, host order (compare against native memory layout)
	data l_h;
	l_h.set_byte_order(data::HOST_ORDER);
	uint16_t l_v16 = 0x1234;
	uint32_t l_v32 = 0x01020304;
	uint64_t l_v64 = 0x0102030405060708ULL;
	l_h.append_uint8(0x7f);
	l_h.append_uint16(l_v16);
	l_h.append_uint32(l_v32);
	l_h.append_uint64(l_v64);
	std::vector<uint8_t> l_native = { 0x7f };
	const uint8_t *l_p;
	l_p = reinterpret_cast<const uint8_t *>(&l_v16);
	l_native.insert(l_native.end(), l_p, l_p + sizeof(l_v16));
	l_p = reinterpret_cast<const uint8_t *>(&l_v32);
	l_native.insert(l_native.end(), l_p, l_p + sizeof(l_v32));
	l_p = reinterpret_cast<const uint8_t *>(&l_v64);
	l_native.insert(l_native.end(), l_p, l_p + sizeof(l_v64));
	check(l_h.bytes() == l_native, "host order integers match native memory layout");

	// switching order affects only subsequent writes
	data l_s;
	l_s.set_byte_order(data::HOST_ORDER);
	l_s.append_uint16(0x1234);
	l_s.set_byte_order(data::NETWORK_ORDER);
	l_s.append_uint16(0x1234);
	check(l_s.bytes()[2] == 0x12 && l_s.bytes()[3] == 0x34, "order change applies to subsequent appends");
	check(l_s.bytes()[0] == l_h.bytes()[1] && l_s.bytes()[1] == l_h.bytes()[2],
	    "earlier appends keep the previous order");

	// floating point appends, network order (IEEE 754 big endian)
	data l_f;
	l_f.append_float(-1.5f);
	check(l_f.bytes() == std::vector<uint8_t>({ 0xbf, 0xc0, 0x00, 0x00 }),
	    "network order float is big endian");
	l_f.clear();
	l_f.append_double(-1.5);
	check(l_f.bytes() == std::vector<uint8_t>({ 0xbf, 0xf8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }),
	    "network order double is big endian");
	l_f.clear();
	l_f.append_long_double(-1.5L);
	check(l_f.bytes() == std::vector<uint8_t>({ 0xbf, 0xff, 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }),
	    "network order long double is 10 byte big endian extended");

	// floating point appends, host order (compare against native memory layout)
	data l_fh;
	l_fh.set_byte_order(data::HOST_ORDER);
	float l_vf = -1.5f;
	double l_vd = -1.5;
	long double l_vld = -1.5L;
	l_fh.append_float(l_vf);
	l_fh.append_double(l_vd);
	l_fh.append_long_double(l_vld);
	std::vector<uint8_t> l_fnative;
	l_p = reinterpret_cast<const uint8_t *>(&l_vf);
	l_fnative.insert(l_fnative.end(), l_p, l_p + sizeof(l_vf));
	l_p = reinterpret_cast<const uint8_t *>(&l_vd);
	l_fnative.insert(l_fnative.end(), l_p, l_p + sizeof(l_vd));
	l_p = reinterpret_cast<const uint8_t *>(&l_vld);
	l_fnative.insert(l_fnative.end(), l_p, l_p + 10);
	check(l_fh.size() == 4 + 8 + 10, "host order float/double/long double total size is 22");
	check(l_fh.bytes() == l_fnative, "host order floating point matches native memory layout");

	// display
	const uint8_t l_disp[] = { 0x41, 0x42, 0x00, 0xff };
	data l_dd(l_disp, sizeof(l_disp));
	std::ostringstream l_os;
	l_dd.display(l_os);
	check(l_os.str() == "00000000  41 42 00 ff " + std::string(36, ' ') + " |AB..|\n",
	    "display formats a partial row with offset, hex and ascii");

	data l_empty_d;
	std::ostringstream l_os_empty;
	l_empty_d.display(l_os_empty);
	check(l_os_empty.str().empty(), "display of empty data prints nothing");

	data l_two;
	for (int i = 0; i < 17; i++)
		l_two.append_uint8(static_cast<uint8_t>(i));
	std::ostringstream l_os_two;
	l_two.display(l_os_two);
	const std::string l_two_s = l_os_two.str();
	check(l_two_s.find("\n00000010  10 ") != std::string::npos, "display wraps at 16 bytes per row");

	std::cout << "sample display:" << std::endl;
	l_two.display();
}

template <typename F>
static bool throws_out_of_range(F a_fn)
{
	try {
		a_fn();
	} catch (const std::out_of_range&) {
		return true;
	}
	return false;
}

static void run_cursor_tests()
{
	typedef std::vector<uint8_t> bv;

	// write cursor tracks the end after appends
	data l_a;
	check(l_a.get_write_cursor() == 0 && l_a.get_read_cursor() == 0, "cursors start at 0");
	const uint8_t l_seven[] = { 1, 2, 3, 4, 5, 6, 7 };
	data l_ctor(l_seven, sizeof(l_seven));
	check(l_ctor.get_write_cursor() == 7 && l_ctor.get_read_cursor() == 0,
	    "constructed 7 byte buffer has write cursor 7, read cursor 0");
	l_a.append_uint16(0x0102);
	l_a.append_uint8(0x03);
	check(l_a.get_write_cursor() == 3, "append leaves write cursor at end");
	l_a.append(l_seven, 2);
	check(l_a.get_write_cursor() == 5, "raw append leaves write cursor at end");

	// write at cursor overwrites
	data l_w;
	l_w.append(l_seven, sizeof(l_seven));
	l_w.set_write_cursor(2);
	check(l_w.get_write_cursor() == 2, "set_write_cursor positions the cursor");
	l_w.write_uint16(0xaabb);
	check(l_w.bytes() == bv({ 1, 2, 0xaa, 0xbb, 5, 6, 7 }), "write_uint16 overwrites at cursor");
	check(l_w.get_write_cursor() == 4 && l_w.size() == 7, "write advances cursor without growing");
	l_w.write_uint8(0xcc);
	check(l_w.bytes() == bv({ 1, 2, 0xaa, 0xbb, 0xcc, 6, 7 }), "write_uint8 overwrites at advanced cursor");

	// write extends past end
	l_w.set_write_cursor(6);
	l_w.write_uint32(0x01020304);
	check(l_w.bytes() == bv({ 1, 2, 0xaa, 0xbb, 0xcc, 6, 1, 2, 3, 4 }),
	    "write overlapping the end extends the data");
	check(l_w.get_write_cursor() == 10, "write cursor lands at new end after extending");

	// append ignores the cursor
	l_w.set_write_cursor(0);
	l_w.append_uint8(0x99);
	check(l_w.size() == 11 && l_w.bytes().back() == 0x99 && l_w.bytes()[0] == 1,
	    "append ignores the write cursor and adds at the end");
	check(l_w.get_write_cursor() == 11, "append moves write cursor to the end");

	// raw write
	data l_rw;
	l_rw.append(l_seven, sizeof(l_seven));
	l_rw.set_write_cursor(1);
	l_rw.write(l_seven, 2);
	check(l_rw.bytes() == bv({ 1, 1, 2, 4, 5, 6, 7 }), "raw write overwrites at cursor");

	// write of other widths and float types at cursor
	data l_wt;
	for (int i = 0; i < 40; i++)
		l_wt.append_uint8(0);
	l_wt.set_write_cursor(0);
	l_wt.write_uint64(0x0102030405060708ULL);
	l_wt.write_float(-1.5f);
	l_wt.write_double(-1.5);
	l_wt.write_long_double(-1.5L);
	check(l_wt.get_write_cursor() == 8 + 4 + 8 + 10, "write of all widths advances cursor by exact sizes");
	check(l_wt.size() == 40, "writes inside the buffer do not grow it");
	check(l_wt.bytes()[0] == 0x01 && l_wt.bytes()[8] == 0xbf && l_wt.bytes()[12] == 0xbf
	    && l_wt.bytes()[20] == 0xbf && l_wt.bytes()[21] == 0xff, "write types are laid out in network order");

	// cursor bounds
	check(throws_out_of_range([&] { l_w.set_write_cursor(l_w.size() + 1); }),
	    "set_write_cursor past end throws");
	check(throws_out_of_range([&] { l_w.set_read_cursor(l_w.size() + 1); }),
	    "set_read_cursor past end throws");
	l_w.set_write_cursor(l_w.size());
	l_w.set_read_cursor(l_w.size());
	check(l_w.get_write_cursor() == l_w.size() && l_w.get_read_cursor() == l_w.size(),
	    "cursor may be set to exactly size()");

	// reads, round trip in both byte orders
	for (int l_o = 0; l_o < 2; l_o++) {
		data::byte_order l_order = l_o ? data::NETWORK_ORDER : data::HOST_ORDER;
		const char *l_name = l_o ? "network" : "host";
		std::string l_msg;

		data l_r;
		l_r.set_byte_order(l_order);
		l_r.append_uint8(0xfe);
		l_r.append_uint16(0xbeef);
		l_r.append_uint32(0xdeadbeef);
		l_r.append_uint64(0x0123456789abcdefULL);
		l_r.append_float(3.25f);
		l_r.append_double(-1234.5678);
		l_r.append_long_double(2.718281828459045235L);
		l_r.append_uint8(0x55);

		l_msg = std::string(l_name) + " order read_uint8 round trip";
		check(l_r.read_uint8() == 0xfe, l_msg.c_str());
		l_msg = std::string(l_name) + " order read_uint16 round trip";
		check(l_r.read_uint16() == 0xbeef, l_msg.c_str());
		l_msg = std::string(l_name) + " order read_uint32 round trip";
		check(l_r.read_uint32() == 0xdeadbeef, l_msg.c_str());
		l_msg = std::string(l_name) + " order read_uint64 round trip";
		check(l_r.read_uint64() == 0x0123456789abcdefULL, l_msg.c_str());
		l_msg = std::string(l_name) + " order read_float round trip";
		check(l_r.read_float() == 3.25f, l_msg.c_str());
		l_msg = std::string(l_name) + " order read_double round trip";
		check(l_r.read_double() == -1234.5678, l_msg.c_str());
		l_msg = std::string(l_name) + " order read_long_double round trip";
		check(l_r.read_long_double() == 2.718281828459045235L, l_msg.c_str());
		l_msg = std::string(l_name) + " order reads advance cursor through whole buffer";
		check(l_r.get_read_cursor() == l_r.size() - 1 && l_r.read_uint8() == 0x55, l_msg.c_str());
		check(throws_out_of_range([&] { l_r.read_uint8(); }), "read past end throws");
	}

	// read cursor positioning and raw read
	data l_rd;
	l_rd.append(l_seven, sizeof(l_seven));
	l_rd.set_read_cursor(3);
	check(l_rd.read_uint8() == 4 && l_rd.get_read_cursor() == 4, "set_read_cursor then read_uint8");
	uint8_t l_out[3];
	l_rd.read(l_out, 3);
	check(l_out[0] == 5 && l_out[2] == 7 && l_rd.get_read_cursor() == 7, "raw read copies bytes and advances");
	l_rd.set_read_cursor(5);
	check(throws_out_of_range([&] { l_rd.read_uint32(); }), "read_uint32 with too few bytes throws");
	check(l_rd.get_read_cursor() == 5, "failed read does not move read cursor");
	check(throws_out_of_range([&] { l_rd.read_long_double(); }), "read_long_double with too few bytes throws");

	// read and write cursors are independent
	data l_ind;
	l_ind.append_uint32(0x01020304);
	l_ind.read_uint16();
	check(l_ind.get_read_cursor() == 2 && l_ind.get_write_cursor() == 4, "read and write cursors are independent");

	// write then read through cursors
	data l_wr;
	for (int i = 0; i < 8; i++)
		l_wr.append_uint8(0);
	l_wr.set_write_cursor(2);
	l_wr.write_uint16(0xcafe);
	l_wr.set_read_cursor(2);
	check(l_wr.read_uint16() == 0xcafe, "value written at cursor reads back at same position");

	// clear resets cursors
	l_wr.clear();
	check(l_wr.get_write_cursor() == 0 && l_wr.get_read_cursor() == 0, "clear resets both cursors");
}

static void run_string_tests()
{
	typedef std::vector<uint8_t> bv;

	// append_string layout: uint32 length then raw bytes
	data l_a;
	l_a.append_string("hi");
	check(l_a.bytes() == bv({ 0x00, 0x00, 0x00, 0x02, 0x68, 0x69 }), "network order string is big endian length + bytes");
	check(l_a.get_write_cursor() == 6, "append_string leaves write cursor at end");

	data l_h;
	l_h.set_byte_order(data::HOST_ORDER);
	l_h.append_string("hi");
	data l_ref;
	l_ref.set_byte_order(data::HOST_ORDER);
	l_ref.append_uint32(2);
	l_ref.append_uint8('h');
	l_ref.append_uint8('i');
	check(l_h.bytes() == l_ref.bytes(), "host order string length prefix follows host order");

	// read_string round trip, including empty and embedded NUL
	data l_r;
	const std::string l_nul("a\0b", 3);
	l_r.append_string("hello");
	l_r.append_string("");
	l_r.append_string(l_nul);
	l_r.append_uint8(0x7e);
	check(l_r.read_string() == "hello", "read_string round trip");
	check(l_r.read_string().empty(), "read_string of empty string");
	check(l_r.read_string() == l_nul, "read_string preserves embedded NUL");
	check(l_r.read_uint8() == 0x7e, "read_string leaves cursor just past the string");

	// write_string at cursor
	data l_w;
	for (int i = 0; i < 10; i++)
		l_w.append_uint8(0xff);
	l_w.set_write_cursor(1);
	l_w.write_string("ab");
	check(l_w.bytes() == bv({ 0xff, 0, 0, 0, 2, 'a', 'b', 0xff, 0xff, 0xff }), "write_string overwrites at cursor");
	check(l_w.get_write_cursor() == 7 && l_w.size() == 10, "write_string advances cursor without growing");
	l_w.set_write_cursor(9);
	l_w.write_string("xyz");
	check(l_w.size() == 9 + 4 + 3 && l_w.get_write_cursor() == 16, "write_string past end extends the data");
	l_w.set_read_cursor(9);
	check(l_w.read_string() == "xyz", "string written at cursor reads back");

	// truncated reads
	data l_t;
	l_t.append_uint16(0x0001);
	check(throws_out_of_range([&] { l_t.read_string(); }), "read_string with truncated length prefix throws");
	data l_t2;
	l_t2.append_uint32(10);
	l_t2.append_uint8('x');
	check(throws_out_of_range([&] { l_t2.read_string(); }), "read_string with truncated body throws");
	check(l_t2.get_read_cursor() == 0, "failed read_string leaves read cursor unmoved");
}

static void run_arbitrary_tests()
{
	// append / read at every width, both byte orders
	for (int l_net = 0; l_net < 2; l_net++) {
		data l_d;
		l_d.set_byte_order(l_net ? data::NETWORK_ORDER : data::HOST_ORDER);
		bool l_ok = true;
		for (unsigned l_bits = 8; l_bits <= 64; l_bits += 8) {
			uint64_t l_val = 0x0123456789abcdefULL >> (64 - l_bits);
			l_d.append_arbitrary(l_val, l_bits);
			l_ok = l_ok && l_d.read_arbitrary(l_bits) == l_val;
		}
		check(l_ok && l_d.size() == 288 / 8, l_net ? "append_arbitrary/read_arbitrary round trip, network order" : "append_arbitrary/read_arbitrary round trip, host order");
	}

	// byte layout
	data l_n;
	l_n.append_arbitrary(0x123456, 24);
	check(l_n.size() == 3 && l_n.bytes()[0] == 0x12 && l_n.bytes()[2] == 0x56, "append_arbitrary 24 bits is big endian in network order");
	data l_h;
	l_h.set_byte_order(data::HOST_ORDER);
	l_h.append_arbitrary(0x123456, 24);
	check(l_h.bytes()[0] == 0x56 && l_h.bytes()[2] == 0x12, "append_arbitrary 24 bits is little endian in host order");
	check(l_n.read_arbitrary(24) == 0x123456 && l_n.get_read_cursor() == 3, "read_arbitrary advances read cursor");

	// write at cursor
	data l_w;
	l_w.append_arbitrary(0xffffffffffffULL, 48);
	l_w.set_write_cursor(1);
	l_w.write_arbitrary(0x0102030405ULL, 40);
	check(l_w.size() == 6 && l_w.bytes()[0] == 0xff && l_w.bytes()[1] == 0x01 && l_w.bytes()[5] == 0x05, "write_arbitrary overwrites at cursor");
	check(l_w.get_write_cursor() == 6, "write_arbitrary advances write cursor");
	l_w.write_arbitrary(0xabcdef, 24);
	check(l_w.size() == 9 && l_w.get_write_cursor() == 9, "write_arbitrary extends buffer");

	// errors
	auto l_throws = [](auto a_fn) {
		try { a_fn(); } catch (const std::invalid_argument&) { return true; }
		return false;
	};
	data l_e;
	check(l_throws([&] { l_e.append_arbitrary(1, 12); }), "append_arbitrary rejects width not a multiple of 8");
	check(l_throws([&] { l_e.append_arbitrary(1, 0); }), "append_arbitrary rejects width 0");
	check(l_throws([&] { l_e.write_arbitrary(1, 72); }), "write_arbitrary rejects width above 64");
	check(l_throws([&] { l_e.append_arbitrary(0x1000000, 24); }), "append_arbitrary rejects value too large for width");
	check(l_throws([&] { l_e.write_arbitrary(0x100, 8); }), "write_arbitrary rejects value too large for width");
	check(l_e.size() == 0, "rejected writes leave data unchanged");
	l_e.append_arbitrary(0xabcdef, 24);
	check(l_throws([&] { l_e.read_arbitrary(7); }) && l_e.get_read_cursor() == 0, "read_arbitrary rejects bad width");
	bool l_oor = false;
	try { l_e.read_arbitrary(32); } catch (const std::out_of_range&) { l_oor = true; }
	check(l_oor && l_e.get_read_cursor() == 0, "read_arbitrary past end throws out_of_range, cursor unmoved");
}

// prints input and packed sizes, packed size as a percentage of input, and the ratio input:packed
static void report_compression(const char *a_label, size_t a_in, size_t a_packed)
{
	std::cout << "  " << a_label << ": input " << a_in << " bytes, packed " << a_packed << " bytes";
	if (a_in && a_packed) {
		char l_buf[64];
		std::snprintf(l_buf, sizeof(l_buf), ", packed is %.1f%% of input, ratio %.2f:1",
			100.0 * a_packed / a_in, static_cast<double>(a_in) / a_packed);
		std::cout << l_buf;
	}
	std::cout << std::endl;
}

static bool huffman_round_trip(const char *a_label, const data& a_src)
{
	data l_packed = huffman::compress(a_src);
	report_compression(a_label, a_src.size(), l_packed.size());
	data l_back = huffman::decompress(l_packed);
	return l_back.bytes() == a_src.bytes();
}

static void run_huffman_tests()
{
	// empty
	data l_empty;
	check(huffman::compress(l_empty).size() == 8, "huffman: empty data compresses to the 8 byte header");
	check(huffman_round_trip("empty", l_empty), "huffman: empty data round trips");

	// single byte, and a run of one value
	data l_one;
	l_one.append_uint8(0x42);
	check(huffman_round_trip("single byte", l_one), "huffman: single byte round trips");
	data l_run(std::string(1000, 'x'));
	check(huffman_round_trip("run of 1000 x", l_run), "huffman: run of one value round trips");
	check(huffman::compress(l_run).size() < 20, "huffman: run of one value is tiny");

	// two distinct values
	data l_two(std::string("abababbbbaaab"));
	check(huffman_round_trip("two values", l_two), "huffman: two distinct values round trip");

	// text compresses and round trips
	std::string l_text;
	for (int i = 0; i < 200; i++)
		l_text += "the quick brown fox jumps over the lazy dog. ";
	data l_txt(l_text);
	data l_txt_packed = huffman::compress(l_txt);
	report_compression("repetitive text", l_txt.size(), l_txt_packed.size());
	check(l_txt_packed.size() < l_txt.size() * 6 / 10, "huffman: text shrinks below 60% of original size");
	check(huffman::decompress(l_txt_packed).bytes() == l_txt.bytes(), "huffman: text round trips");

	// all 256 byte values, uniform
	data l_all;
	for (int i = 0; i < 256 * 4; i++)
		l_all.append_uint8(static_cast<uint8_t>(i));
	check(huffman_round_trip("all byte values", l_all), "huffman: all byte values round trip");

	// skewed distribution
	data l_skew;
	for (int i = 0; i < 5000; i++)
		l_skew.append_uint8(i % 50 == 0 ? static_cast<uint8_t>(i / 50) : 0);
	data l_skew_packed = huffman::compress(l_skew);
	report_compression("skewed data", l_skew.size(), l_skew_packed.size());
	check(l_skew_packed.size() < l_skew.size() / 4, "huffman: skewed data shrinks");
	check(huffman::decompress(l_skew_packed).bytes() == l_skew.bytes(), "huffman: skewed data round trips");

	// pseudo-random bytes
	data l_rnd;
	uint32_t l_state = 12345;
	for (int i = 0; i < 10000; i++) {
		l_state = l_state * 1664525u + 1013904223u;
		l_rnd.append_uint8(static_cast<uint8_t>(l_state >> 24));
	}
	check(huffman_round_trip("random bytes", l_rnd), "huffman: random bytes round trip");

	// fibonacci frequencies give the deepest possible tree (long codes)
	data l_fib;
	uint64_t l_a = 1, l_b = 1;
	for (int l_sym = 0; l_sym < 24; l_sym++) {
		for (uint64_t i = 0; i < l_a; i++)
			l_fib.append_uint8(static_cast<uint8_t>(l_sym));
		uint64_t l_t = l_a + l_b;
		l_a = l_b;
		l_b = l_t;
	}
	check(huffman_round_trip("deep tree (fibonacci)", l_fib), "huffman: deep code tree round trips");

	// frequency larger than 255 and 65535 exercises wider header fields
	data l_big(std::string(70000, 'a') + std::string(3, 'b'));
	check(huffman_round_trip("large frequencies", l_big), "huffman: large frequencies round trip");

	// input is not modified, and its byte order and cursors do not matter
	data l_in(std::string("hello huffman"));
	l_in.set_byte_order(data::HOST_ORDER);
	l_in.set_read_cursor(5);
	std::vector<uint8_t> l_before = l_in.bytes();
	data l_out = huffman::compress(l_in);
	check(l_in.bytes() == l_before && l_in.get_read_cursor() == 5, "huffman: compress leaves input unchanged");
	l_out.set_byte_order(data::HOST_ORDER);
	l_out.set_read_cursor(3);
	check(huffman::decompress(l_out).bytes() == l_before, "huffman: decompress ignores input byte order and cursor");

	// corrupt input
	auto l_rejects = [](const data& a_bad) {
		try { huffman::decompress(a_bad); } catch (const std::runtime_error&) { return true; }
		return false;
	};
	data l_good = huffman::compress(l_txt);
	data l_cut(l_good.bytes().data(), l_good.size() - 10);
	check(l_rejects(l_cut), "huffman: truncated code data is rejected");
	data l_hdr(l_good.bytes().data(), 5);
	check(l_rejects(l_hdr), "huffman: truncated header is rejected");
	check(l_rejects(data()), "huffman: empty input is rejected");
	data l_bad = l_good;
	std::vector<uint8_t> l_mut = l_good.bytes();
	l_mut[7] ^= 1;	// original size no longer matches the frequencies
	check(l_rejects(data(l_mut.data(), l_mut.size())), "huffman: size mismatch is rejected");
}

static data make_random(size_t a_len, uint32_t a_seed)
{
	data l_d;
	uint32_t l_state = a_seed;
	for (size_t i = 0; i < a_len; i++) {
		l_state = l_state * 1664525u + 1013904223u;
		l_d.append_uint8(static_cast<uint8_t>(l_state >> 24));
	}
	return l_d;
}

static bool lzma_round_trip(const char *a_label, const data& a_src)
{
	data l_packed = lzma::compress(a_src);
	report_compression(a_label, a_src.size(), l_packed.size());
	data l_back = lzma::decompress(l_packed);
	return l_back.bytes() == a_src.bytes();
}

static void run_lzma_tests()
{
	// empty: 13 byte header plus the 5 byte range coder flush
	data l_empty;
	check(lzma::compress(l_empty).size() == 18, "lzma: empty data compresses to header and flush");
	check(lzma_round_trip("empty", l_empty), "lzma: empty data round trips");

	// header layout, little endian
	data l_hdr_src(std::string(300, 'h'));
	data l_hdr = lzma::compress(l_hdr_src);
	check(l_hdr.bytes()[0] == 0x5d, "lzma: properties byte is lc 3, lp 0, pb 2");
	check(l_hdr.bytes()[1] == 0x00 && l_hdr.bytes()[2] == 0x10 && l_hdr.bytes()[3] == 0x00 && l_hdr.bytes()[4] == 0x00, "lzma: dictionary size is a power of two, little endian");
	check(l_hdr.bytes()[5] == 0x2c && l_hdr.bytes()[6] == 0x01 && l_hdr.bytes()[12] == 0x00, "lzma: uncompressed size is stored little endian");
	check(l_hdr.bytes()[13] == 0x00, "lzma: range coder stream starts with a zero byte");

	// single byte, runs, two values
	data l_one;
	l_one.append_uint8(0x42);
	check(lzma_round_trip("single byte", l_one), "lzma: single byte round trips");
	data l_run(std::string(100000, 'x'));
	check(lzma_round_trip("run of 100000 x", l_run), "lzma: long run round trips");
	check(lzma::compress(l_run).size() < 200, "lzma: long run is tiny");
	data l_two(std::string("abababbbbaaab"));
	check(lzma_round_trip("two values", l_two), "lzma: two distinct values round trip");

	// text
	std::string l_text;
	for (int i = 0; i < 200; i++)
		l_text += "the quick brown fox jumps over the lazy dog. ";
	data l_txt(l_text);
	data l_txt_packed = lzma::compress(l_txt);
	report_compression("repetitive text", l_txt.size(), l_txt_packed.size());
	check(l_txt_packed.size() < 200, "lzma: repetitive text is tiny");
	check(lzma::decompress(l_txt_packed).bytes() == l_txt.bytes(), "lzma: text round trips");

	// varied text with matches at many distances, including rep matches and literals after matches
	std::string l_prose;
	const char *l_words[] = { "alpha", "beta", "gamma", "delta", "epsilon", "zeta", "eta", "theta", "iota", "kappa" };
	uint32_t l_seed = 99;
	for (int i = 0; i < 4000; i++) {
		l_seed = l_seed * 1664525u + 1013904223u;
		l_prose += l_words[(l_seed >> 24) % 10];
		l_prose += (l_seed >> 16) % 7 == 0 ? ". " : " ";
	}
	data l_pro(l_prose);
	check(lzma_round_trip("word salad", l_pro), "lzma: word salad round trips");
	check(lzma::compress(l_pro).size() < l_pro.size() / 3, "lzma: word salad shrinks below a third");

	// all byte values, skewed data, random bytes
	data l_all;
	for (int i = 0; i < 256 * 4; i++)
		l_all.append_uint8(static_cast<uint8_t>(i));
	check(lzma_round_trip("all byte values", l_all), "lzma: all byte values round trip");

	data l_skew;
	for (int i = 0; i < 5000; i++)
		l_skew.append_uint8(i % 50 == 0 ? static_cast<uint8_t>(i / 50) : 0);
	check(lzma_round_trip("skewed data", l_skew), "lzma: skewed data round trips");

	data l_rnd = make_random(10000, 12345);
	check(lzma_round_trip("random bytes", l_rnd), "lzma: random bytes round trip");
	check(lzma::compress(l_rnd).size() < l_rnd.size() + l_rnd.size() / 20, "lzma: random bytes expand by under 5%");

	// distances well past 128 use direct bits and the alignment coder; past 64 KiB uses the top slots
	data l_blk = make_random(5000, 777);
	data l_far(l_blk);
	l_far.append(make_random(3000, 31).bytes().data(), 3000);
	l_far.append(l_blk.bytes().data(), l_blk.size());
	check(lzma_round_trip("far repeat (5000 byte block)", l_far), "lzma: far repeat round trips");
	check(lzma::compress(l_far).size() < 8500, "lzma: far repeat is found and shrinks");
	data l_vfar(l_blk);
	l_vfar.append(make_random(200000, 4242).bytes().data(), 200000);
	l_vfar.append(l_blk.bytes().data(), l_blk.size());
	check(lzma_round_trip("very far repeat (200 KB away)", l_vfar), "lzma: very far repeat round trips");
	check(lzma::compress(l_vfar).size() < l_vfar.size(), "lzma: very far repeat is found and shrinks");

	// compress and decompress this project's own sources, if present
	data l_src;
	try {
		l_src.load_file("data.cc");
		check(lzma_round_trip("data.cc", l_src), "lzma: source file round trips");
	} catch (const std::runtime_error&) {
	}

	// stream written by the reference xz encoder: unknown size, terminated by an end marker
	const uint8_t l_ref[] = {
		0x5d, 0x00, 0x00, 0x01, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0x30, 0x98,
		0x88, 0xa7, 0xea, 0x4b, 0xbb, 0x3e, 0x29, 0xdd, 0x1e, 0x72, 0xce, 0x0e, 0xf6, 0x83, 0xde, 0x5a,
		0x74, 0xe0, 0x62, 0x67, 0x71, 0x03, 0x63, 0xb0, 0xe0, 0xc0, 0x9b, 0xf4, 0xba, 0x3f, 0x91, 0xa1,
		0x95, 0xee, 0xe7, 0x2a, 0x95, 0x51, 0xc5, 0xab, 0x3a, 0x01, 0x72, 0x95, 0x9e, 0x35, 0xff, 0xfc,
		0xa3, 0xc0, 0x00
	};
	const std::string l_ref_text = "abcabcabcabc xyz abcabc xyz, the cat sat on the mat; the cat sat on the hat. abc xyz abc xyz abcabcabc!";
	data l_ref_out = lzma::decompress(data(l_ref, sizeof(l_ref)));
	check(l_ref_out.bytes() == std::vector<uint8_t>(l_ref_text.begin(), l_ref_text.end()), "lzma: decodes xz reference stream with end marker");

	// input is not modified, and its byte order and cursors do not matter
	data l_in(std::string("hello lzma hello lzma hello"));
	l_in.set_byte_order(data::HOST_ORDER);
	l_in.set_read_cursor(5);
	std::vector<uint8_t> l_before = l_in.bytes();
	data l_out = lzma::compress(l_in);
	check(l_in.bytes() == l_before && l_in.get_read_cursor() == 5, "lzma: compress leaves input unchanged");
	l_out.set_byte_order(data::HOST_ORDER);
	l_out.set_read_cursor(3);
	check(lzma::decompress(l_out).bytes() == l_before, "lzma: decompress ignores input byte order and cursor");

	// corrupt input
	auto l_rejects = [](const std::vector<uint8_t>& a_bad) {
		try { lzma::decompress(data(a_bad.data(), a_bad.size())); } catch (const std::runtime_error&) { return true; }
		return false;
	};
	std::vector<uint8_t> l_good = lzma::compress(l_pro).bytes();
	check(l_rejects(std::vector<uint8_t>()), "lzma: empty input is rejected");
	check(l_rejects(std::vector<uint8_t>(l_good.begin(), l_good.begin() + 12)), "lzma: truncated header is rejected");
	check(l_rejects(std::vector<uint8_t>(l_good.begin(), l_good.begin() + 15)), "lzma: header without a range coder stream is rejected");
	check(l_rejects(std::vector<uint8_t>(l_good.begin(), l_good.begin() + l_good.size() / 2)), "lzma: truncated stream is rejected");
	std::vector<uint8_t> l_bad = l_good;
	l_bad[0] = 225;
	check(l_rejects(l_bad), "lzma: invalid properties byte is rejected");
	l_bad = l_good;
	l_bad[13] = 1;
	check(l_rejects(l_bad), "lzma: nonzero first range coder byte is rejected");
}

// compresses every regular file in a directory with both coders, verifies each round trip and prints a comparison table
static void run_comp_dir_tests(const char *a_dir)
{
	namespace fs = std::filesystem;

	std::vector<fs::path> l_files;
	std::error_code l_ec;
	for (fs::directory_iterator l_it(a_dir, l_ec), l_end; !l_ec && l_it != l_end; l_it.increment(l_ec)) {
		if (l_it->is_regular_file())
			l_files.push_back(l_it->path());
	}
	if (l_ec || l_files.empty()) {
		std::cout << "SKIP: no files found in " << a_dir << std::endl;
		return;
	}
	std::sort(l_files.begin(), l_files.end());

	struct result {
		std::string name;
		size_t in, huff, lzma;
		bool huff_ok, lzma_ok;
	};
	std::vector<result> l_results;
	for (const fs::path& l_path : l_files) {
		data l_src;
		l_src.load_file(l_path.string());
		data l_huff = huffman::compress(l_src);
		data l_lzma = lzma::compress(l_src);
		l_results.push_back({ l_path.filename().string(), l_src.size(), l_huff.size(), l_lzma.size(),
			huffman::decompress(l_huff).bytes() == l_src.bytes(), lzma::decompress(l_lzma).bytes() == l_src.bytes() });
	}

	// packed size, percentage of input and ratio input:packed
	auto l_cols = [](size_t a_in, size_t a_packed) {
		char l_buf[64];
		std::snprintf(l_buf, sizeof(l_buf), "%10zu %6.1f%% %6.2f:1", a_packed,
			a_in ? 100.0 * a_packed / a_in : 0.0, a_packed ? static_cast<double>(a_in) / a_packed : 0.0);
		return std::string(l_buf);
	};

	std::cout << "compression of files in " << a_dir << ":" << std::endl;
	std::printf("  %-20s %10s | %10s %7s %8s | %10s %7s %8s\n", "file", "input", "huffman", "%", "ratio", "lzma", "%", "ratio");
	size_t l_total_in = 0, l_total_huff = 0, l_total_lzma = 0;
	for (const result& l_r : l_results) {
		std::printf("  %-20s %10zu | %s | %s\n", l_r.name.c_str(), l_r.in, l_cols(l_r.in, l_r.huff).c_str(), l_cols(l_r.in, l_r.lzma).c_str());
		l_total_in += l_r.in;
		l_total_huff += l_r.huff;
		l_total_lzma += l_r.lzma;
	}
	std::printf("  %-20s %10zu | %s | %s\n", "total", l_total_in, l_cols(l_total_in, l_total_huff).c_str(), l_cols(l_total_in, l_total_lzma).c_str());

	for (const result& l_r : l_results) {
		check(l_r.huff_ok, ("comp dir: huffman round trips " + l_r.name).c_str());
		check(l_r.lzma_ok, ("comp dir: lzma round trips " + l_r.name).c_str());
	}
}

static void run_file_tests()
{
	const std::string l_path = "data-ssai-test.tmp";

	data l_d;
	l_d.append_uint16(0x1234);
	l_d.append_string(std::string("hi\0there", 8));
	l_d.save_file(l_path);

	data l_in;
	l_in.append_uint8(0xff);	// replaced by load
	l_in.load_file(l_path);
	check(l_in.bytes() == l_d.bytes(), "file round trip preserves bytes");
	check(l_in.get_write_cursor() == l_in.size() && l_in.get_read_cursor() == 0, "load_file sets cursors");
	check(l_in.read_uint16() == 0x1234, "loaded data is readable");

	data l_empty;
	l_empty.save_file(l_path);
	l_in.load_file(l_path);
	check(l_in.size() == 0, "empty data round trips");

	bool l_threw = false;
	data l_keep;
	l_keep.append_uint8(7);
	try { l_keep.load_file("no-such-dir/no-such-file"); } catch (const std::runtime_error&) { l_threw = true; }
	check(l_threw && l_keep.size() == 1, "load_file of missing file throws and leaves data unchanged");

	l_threw = false;
	try { l_keep.save_file("no-such-dir/no-such-file"); } catch (const std::runtime_error&) { l_threw = true; }
	check(l_threw, "save_file to bad path throws");

	std::remove(l_path.c_str());
}

void personal_tests() {
	data l_pers;
	l_pers.write_double(2.7818281828459045);
	l_pers.write_uint64(0xdeadbeefc0edbabe);
	l_pers.display();
	l_pers.save_file("personal.data");
}

int main(int, char **)
{
	run_tests();
	run_cursor_tests();
	run_string_tests();
	run_arbitrary_tests();
	run_file_tests();
	run_huffman_tests();
	run_lzma_tests();
	run_comp_dir_tests("comp_test_cases");
	std::cout << (g_failures ? "tests FAILED" : "all tests passed") << std::endl;
	personal_tests();
	return g_failures ? 1 : 0;
}
