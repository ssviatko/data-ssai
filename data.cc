#include "data.h"

#include <algorithm>
#include <cfloat>
#include <cstring>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <utility>
#include <string>

static_assert(sizeof(float) == 4 && FLT_MANT_DIG == 24, "float must be IEEE 754 binary32");
static_assert(sizeof(double) == 8 && DBL_MANT_DIG == 53, "double must be IEEE 754 binary64");
static_assert(LDBL_MANT_DIG == 64, "long double must be x87 80-bit extended");

// x87 extended precision occupies the low 10 bytes of a long double
static const size_t LONG_DOUBLE_BYTES = 10;

data::data()
{
}

data::data(const uint8_t *a_bytes, size_t a_len)
	: m_bytes(a_bytes, a_bytes + a_len), m_wcursor(a_len)
{
}

data::data(const std::string& a_str)
	: m_bytes(a_str.begin(), a_str.end()), m_wcursor(a_str.size())
{
}

data::~data()
{
}

void data::set_byte_order(byte_order a_order)
{
	m_order = a_order;
}

data::byte_order data::get_byte_order() const
{
	return m_order;
}

void data::set_write_cursor(size_t a_pos)
{
	if (a_pos > m_bytes.size())
		throw std::out_of_range("write cursor beyond end of data");
	m_wcursor = a_pos;
}

size_t data::get_write_cursor() const
{
	return m_wcursor;
}

void data::set_read_cursor(size_t a_pos)
{
	if (a_pos > m_bytes.size())
		throw std::out_of_range("read cursor beyond end of data");
	m_rcursor = a_pos;
}

size_t data::get_read_cursor() const
{
	return m_rcursor;
}

// ---- internals ----

static bool use_big_endian(data::byte_order a_order)
{
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
	const bool l_host_big = true;
#else
	const bool l_host_big = false;
#endif
	return a_order == data::NETWORK_ORDER || l_host_big;
}

// copy into the buffer at a_pos, extending it if needed, and leave the write cursor after the copy
void data::store(const uint8_t *a_src, size_t a_len, size_t a_pos)
{
	if (a_pos + a_len > m_bytes.size())
		m_bytes.resize(a_pos + a_len);
	if (a_len)
		std::memcpy(&m_bytes[a_pos], a_src, a_len);
	m_wcursor = a_pos + a_len;
}

// byte count for a bit width, which must be a multiple of 8 from 8 to 64
size_t data::int_width(unsigned a_bits)
{
	if (a_bits == 0 || a_bits > 64 || a_bits % 8 != 0)
		throw std::invalid_argument("integer bit width must be a multiple of 8 from 8 to 64");
	return a_bits / 8;
}

void data::check_fits(uint64_t a_val, size_t a_width)
{
	if (a_width < 8 && (a_val >> (a_width * 8)) != 0)
		throw std::invalid_argument("value does not fit in the requested bit width");
}

void data::put_int(uint64_t a_val, size_t a_width, size_t a_pos)
{
	uint8_t l_buf[8];
	bool l_big = use_big_endian(m_order);

	for (size_t i = 0; i < a_width; i++) {
		size_t l_shift = l_big ? (a_width - 1 - i) * 8 : i * 8;
		l_buf[i] = static_cast<uint8_t>(a_val >> l_shift);
	}
	store(l_buf, a_width, a_pos);
}

void data::put_long_double(long double a_val, size_t a_pos)
{
	// x87 extended is little endian in memory: 8 byte mantissa, then 2 byte sign/exponent.
	// Only the low 10 bytes are significant; the rest is padding.
	uint8_t l_raw[LONG_DOUBLE_BYTES];
	std::memcpy(l_raw, &a_val, sizeof(l_raw));

	if (m_order == NETWORK_ORDER)
		std::reverse(l_raw, l_raw + sizeof(l_raw));

	store(l_raw, sizeof(l_raw), a_pos);
}

void data::put_string(const std::string& a_val, size_t a_pos)
{
	if (a_val.size() > UINT32_MAX)
		throw std::length_error("string too long for uint32 length prefix");

	put_int(a_val.size(), 4, a_pos);
	store(reinterpret_cast<const uint8_t *>(a_val.data()), a_val.size(), a_pos + 4);
}

void data::check_read(size_t a_len) const
{
	if (m_rcursor + a_len > m_bytes.size())
		throw std::out_of_range("read past end of data");
}

uint64_t data::get_int(size_t a_width)
{
	check_read(a_width);

	uint64_t l_val = 0;
	bool l_big = use_big_endian(m_order);

	for (size_t i = 0; i < a_width; i++) {
		uint64_t l_byte = m_bytes[m_rcursor + i];
		if (l_big)
			l_val = (l_val << 8) | l_byte;
		else
			l_val |= l_byte << (i * 8);
	}
	m_rcursor += a_width;
	return l_val;
}

// ---- append ----

void data::append(const uint8_t *a_bytes, size_t a_len)
{
	store(a_bytes, a_len, m_bytes.size());
}

void data::append_uint8(uint8_t a_val)
{
	put_int(a_val, 1, m_bytes.size());
}

void data::append_uint16(uint16_t a_val)
{
	put_int(a_val, 2, m_bytes.size());
}

void data::append_uint32(uint32_t a_val)
{
	put_int(a_val, 4, m_bytes.size());
}

void data::append_uint64(uint64_t a_val)
{
	put_int(a_val, 8, m_bytes.size());
}

void data::append_arbitrary(uint64_t a_val, unsigned a_bits)
{
	size_t l_width = int_width(a_bits);
	check_fits(a_val, l_width);
	put_int(a_val, l_width, m_bytes.size());
}

void data::append_float(float a_val)
{
	uint32_t l_bits;
	std::memcpy(&l_bits, &a_val, sizeof(l_bits));
	put_int(l_bits, 4, m_bytes.size());
}

void data::append_double(double a_val)
{
	uint64_t l_bits;
	std::memcpy(&l_bits, &a_val, sizeof(l_bits));
	put_int(l_bits, 8, m_bytes.size());
}

void data::append_long_double(long double a_val)
{
	put_long_double(a_val, m_bytes.size());
}

void data::append_string(const std::string& a_val)
{
	put_string(a_val, m_bytes.size());
}

// ---- write at cursor ----

void data::write(const uint8_t *a_bytes, size_t a_len)
{
	store(a_bytes, a_len, m_wcursor);
}

void data::write_uint8(uint8_t a_val)
{
	put_int(a_val, 1, m_wcursor);
}

void data::write_uint16(uint16_t a_val)
{
	put_int(a_val, 2, m_wcursor);
}

void data::write_uint32(uint32_t a_val)
{
	put_int(a_val, 4, m_wcursor);
}

void data::write_uint64(uint64_t a_val)
{
	put_int(a_val, 8, m_wcursor);
}

void data::write_arbitrary(uint64_t a_val, unsigned a_bits)
{
	size_t l_width = int_width(a_bits);
	check_fits(a_val, l_width);
	put_int(a_val, l_width, m_wcursor);
}

void data::write_float(float a_val)
{
	uint32_t l_bits;
	std::memcpy(&l_bits, &a_val, sizeof(l_bits));
	put_int(l_bits, 4, m_wcursor);
}

void data::write_double(double a_val)
{
	uint64_t l_bits;
	std::memcpy(&l_bits, &a_val, sizeof(l_bits));
	put_int(l_bits, 8, m_wcursor);
}

void data::write_long_double(long double a_val)
{
	put_long_double(a_val, m_wcursor);
}

void data::write_string(const std::string& a_val)
{
	put_string(a_val, m_wcursor);
}

// ---- read at cursor ----

void data::read(uint8_t *a_bytes, size_t a_len)
{
	check_read(a_len);
	if (a_len)
		std::memcpy(a_bytes, &m_bytes[m_rcursor], a_len);
	m_rcursor += a_len;
}

uint8_t data::read_uint8()
{
	return static_cast<uint8_t>(get_int(1));
}

uint16_t data::read_uint16()
{
	return static_cast<uint16_t>(get_int(2));
}

uint32_t data::read_uint32()
{
	return static_cast<uint32_t>(get_int(4));
}

uint64_t data::read_uint64()
{
	return get_int(8);
}

uint64_t data::read_arbitrary(unsigned a_bits)
{
	return get_int(int_width(a_bits));
}

float data::read_float()
{
	uint32_t l_bits = static_cast<uint32_t>(get_int(4));
	float l_val;
	std::memcpy(&l_val, &l_bits, sizeof(l_val));
	return l_val;
}

double data::read_double()
{
	uint64_t l_bits = get_int(8);
	double l_val;
	std::memcpy(&l_val, &l_bits, sizeof(l_val));
	return l_val;
}

long double data::read_long_double()
{
	check_read(LONG_DOUBLE_BYTES);

	uint8_t l_raw[LONG_DOUBLE_BYTES];
	std::memcpy(l_raw, &m_bytes[m_rcursor], sizeof(l_raw));
	m_rcursor += sizeof(l_raw);

	if (m_order == NETWORK_ORDER)
		std::reverse(l_raw, l_raw + sizeof(l_raw));

	long double l_val = 0;	// zero so the padding bytes are defined
	std::memcpy(&l_val, l_raw, sizeof(l_raw));
	return l_val;
}

std::string data::read_string()
{
	size_t l_start = m_rcursor;
	size_t l_len = static_cast<size_t>(get_int(4));

	if (m_rcursor + l_len > m_bytes.size()) {
		m_rcursor = l_start;
		throw std::out_of_range("read past end of data");
	}

	std::string l_val(reinterpret_cast<const char *>(m_bytes.data() + m_rcursor), l_len);
	m_rcursor += l_len;
	return l_val;
}

// ---- circular read ----

data::circular_scope::circular_scope(data& a_data)
	: m_data(a_data), m_saved(a_data.m_rcursor)
{
	m_data.m_rcursor = 0;
}

data::circular_scope::~circular_scope()
{
	if (!m_committed)
		m_data.m_rcursor = m_saved;
}

void data::circular_scope::commit()
{
	size_t l_used = m_data.m_rcursor;
	m_data.m_bytes.erase(m_data.m_bytes.begin(), m_data.m_bytes.begin() + l_used);
	m_data.m_wcursor = m_data.m_wcursor > l_used ? m_data.m_wcursor - l_used : 0;
	m_data.m_rcursor = 0;
	m_committed = true;
}

void data::circular_read(uint8_t *a_bytes, size_t a_len)
{
	circular_scope l_scope(*this);
	read(a_bytes, a_len);
	l_scope.commit();
}

uint8_t data::circular_read_uint8()
{
	circular_scope l_scope(*this);
	uint8_t l_val = read_uint8();
	l_scope.commit();
	return l_val;
}

uint16_t data::circular_read_uint16()
{
	circular_scope l_scope(*this);
	uint16_t l_val = read_uint16();
	l_scope.commit();
	return l_val;
}

uint32_t data::circular_read_uint32()
{
	circular_scope l_scope(*this);
	uint32_t l_val = read_uint32();
	l_scope.commit();
	return l_val;
}

uint64_t data::circular_read_uint64()
{
	circular_scope l_scope(*this);
	uint64_t l_val = read_uint64();
	l_scope.commit();
	return l_val;
}

uint64_t data::circular_read_arbitrary(unsigned a_bits)
{
	circular_scope l_scope(*this);
	uint64_t l_val = read_arbitrary(a_bits);
	l_scope.commit();
	return l_val;
}

float data::circular_read_float()
{
	circular_scope l_scope(*this);
	float l_val = read_float();
	l_scope.commit();
	return l_val;
}

double data::circular_read_double()
{
	circular_scope l_scope(*this);
	double l_val = read_double();
	l_scope.commit();
	return l_val;
}

long double data::circular_read_long_double()
{
	circular_scope l_scope(*this);
	long double l_val = read_long_double();
	l_scope.commit();
	return l_val;
}

std::string data::circular_read_string()
{
	circular_scope l_scope(*this);
	std::string l_val = read_string();
	l_scope.commit();
	return l_val;
}

// ---- misc ----

void data::display(std::ostream& a_os) const
{
	static const char l_hex[] = "0123456789abcdef";
	const size_t l_row = 16;

	for (size_t l_off = 0; l_off < m_bytes.size(); l_off += l_row) {
		std::string l_line;

		for (int l_shift = 28; l_shift >= 0; l_shift -= 4)
			l_line += l_hex[(l_off >> l_shift) & 0xf];
		l_line += "  ";

		for (size_t i = 0; i < l_row; i++) {
			if (l_off + i < m_bytes.size()) {
				uint8_t l_b = m_bytes[l_off + i];
				l_line += l_hex[l_b >> 4];
				l_line += l_hex[l_b & 0xf];
				l_line += ' ';
			} else {
				l_line += "   ";
			}
		}

		l_line += " |";
		for (size_t i = 0; i < l_row && l_off + i < m_bytes.size(); i++) {
			uint8_t l_b = m_bytes[l_off + i];
			l_line += (l_b >= 0x20 && l_b < 0x7f) ? static_cast<char>(l_b) : '.';
		}
		l_line += "|\n";

		a_os << l_line;
	}
}

void data::save_file(const std::string& a_path) const
{
	std::ofstream l_out(a_path, std::ios::binary | std::ios::trunc);
	if (!l_out)
		throw std::runtime_error("cannot open file for writing: " + a_path);
	l_out.write(reinterpret_cast<const char *>(m_bytes.data()), static_cast<std::streamsize>(m_bytes.size()));
	l_out.flush();
	if (!l_out)
		throw std::runtime_error("error writing file: " + a_path);
}

void data::load_file(const std::string& a_path)
{
	std::ifstream l_in(a_path, std::ios::binary);
	if (!l_in)
		throw std::runtime_error("cannot open file for reading: " + a_path);
	std::vector<uint8_t> l_bytes((std::istreambuf_iterator<char>(l_in)), std::istreambuf_iterator<char>());
	if (l_in.bad())
		throw std::runtime_error("error reading file: " + a_path);
	m_bytes = std::move(l_bytes);
	m_wcursor = m_bytes.size();
	m_rcursor = 0;
}

void data::clear()
{
	m_bytes.clear();
	m_wcursor = 0;
	m_rcursor = 0;
}

size_t data::size() const
{
	return m_bytes.size();
}

const std::vector<uint8_t>& data::bytes() const
{
	return m_bytes;
}
