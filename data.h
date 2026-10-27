#ifndef DATA_H
#define DATA_H

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

// Byte array with independent read and write cursors.
//
// Cursors are 0-based offsets. The write cursor may be anywhere from 0 to size();
// a value of size() (e.g. 7 for a 7 byte buffer) means the next write appends.
// Writes at the cursor overwrite existing bytes and extend the buffer when they
// run past the end. Reads past the end throw std::out_of_range, as do cursor
// positions beyond size(). Multi-byte values obey the current byte order.
// Strings are stored as a uint32 byte count followed by the raw bytes (no terminator),
// so they may contain embedded NULs.
class data {
public:
	enum byte_order {
		HOST_ORDER,
		NETWORK_ORDER	// big endian
	};

	data();
	data(const uint8_t *a_bytes, size_t a_len);	// write cursor starts at the end, read cursor at 0
	explicit data(const std::string& a_str);	// copies the string's raw bytes (no length prefix); write cursor starts at the end, read cursor at 0
	~data();

	void set_byte_order(byte_order a_order);
	byte_order get_byte_order() const;

	void set_write_cursor(size_t a_pos);
	size_t get_write_cursor() const;
	void set_read_cursor(size_t a_pos);
	size_t get_read_cursor() const;

	// append: always adds at the end of the data and leaves the write cursor at the new end
	void append(const uint8_t *a_bytes, size_t a_len);
	void append_uint8(uint8_t a_val);
	void append_uint16(uint16_t a_val);
	void append_uint32(uint32_t a_val);
	void append_uint64(uint64_t a_val);
	void append_arbitrary(uint64_t a_val, unsigned a_bits);	// a_bits: multiple of 8, 8 to 64; throws std::invalid_argument otherwise or if a_val does not fit
	void append_float(float a_val);
	void append_double(double a_val);
	void append_long_double(long double a_val);	// x87 80-bit extended, 10 bytes
	void append_string(const std::string& a_val);

	// write: stores at the write cursor and advances it past the written bytes
	void write(const uint8_t *a_bytes, size_t a_len);
	void write_uint8(uint8_t a_val);
	void write_uint16(uint16_t a_val);
	void write_uint32(uint32_t a_val);
	void write_uint64(uint64_t a_val);
	void write_arbitrary(uint64_t a_val, unsigned a_bits);	// as append_arbitrary, at the write cursor
	void write_float(float a_val);
	void write_double(double a_val);
	void write_long_double(long double a_val);
	void write_string(const std::string& a_val);

	// read: fetches from the read cursor and advances it past the bytes read
	void read(uint8_t *a_bytes, size_t a_len);
	uint8_t read_uint8();
	uint16_t read_uint16();
	uint32_t read_uint32();
	uint64_t read_uint64();
	uint64_t read_arbitrary(unsigned a_bits);	// a_bits: multiple of 8, 8 to 64; throws std::invalid_argument otherwise
	float read_float();
	double read_double();
	long double read_long_double();
	std::string read_string();	// throws, leaving the cursor unmoved, if the prefix or body is truncated

	// file I/O: the whole buffer, raw bytes, independent of byte order
	void save_file(const std::string& a_path) const;	// writes all bytes, truncating any existing file; throws std::runtime_error on failure
	void load_file(const std::string& a_path);	// replaces the data with the file's contents, write cursor at the end, read cursor at 0; throws std::runtime_error, leaving the data unchanged, on failure

	void display(std::ostream& a_os = std::cout) const;	// hex dump, 16 bytes per row
	void clear();	// empties the data and resets both cursors to 0
	size_t size() const;
	const std::vector<uint8_t>& bytes() const;

private:
	void store(const uint8_t *a_src, size_t a_len, size_t a_pos);
	static size_t int_width(unsigned a_bits);
	static void check_fits(uint64_t a_val, size_t a_width);
	void put_int(uint64_t a_val, size_t a_width, size_t a_pos);
	void put_long_double(long double a_val, size_t a_pos);
	void put_string(const std::string& a_val, size_t a_pos);
	uint64_t get_int(size_t a_width);
	void check_read(size_t a_len) const;

	std::vector<uint8_t> m_bytes;
	byte_order m_order = NETWORK_ORDER;
	size_t m_wcursor = 0;
	size_t m_rcursor = 0;
};

#endif // DATA_H
