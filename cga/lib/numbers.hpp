#pragma once

#include <boost/multiprecision/cpp_int.hpp>

#include <crypto/cryptopp/osrng.h>

namespace cga
{
/** While this uses CryptoPP do not call any of these functions from global scope, as they depend on global variables inside the CryptoPP library which may not have been initialized yet due to an undefined order for globals in different translation units. To make sure this is not an issue, there should be no ASAN warnings at startup on Mac/Clang in the CryptoPP files. */
class random_pool
{
public:
	static void generate_block (unsigned char * output, size_t size);
	static unsigned generate_word32 (unsigned min, unsigned max);
	static unsigned char generate_byte ();

	template <class Iter>
	static void shuffle (Iter begin, Iter end)
	{
		std::lock_guard<std::mutex> lk (mutex);
		pool.Shuffle (begin, end);
	}

	random_pool () = delete;
	random_pool (random_pool const &) = delete;
	random_pool & operator= (random_pool const &) = delete;

private:
	static std::mutex mutex;
	static CryptoPP::AutoSeededRandomPool pool;
};

using uint128_t = boost::multiprecision::uint128_t;
using uint256_t = boost::multiprecision::uint256_t;
using uint512_t = boost::multiprecision::uint512_t;
// SI dividers
cga::uint128_t const Gcga_ratio = cga::uint128_t ("1000000000000000000000000000000000"); // 10^33
cga::uint128_t const Mcga_ratio = cga::uint128_t ("100000000000000000000000000000"); // 10^29
cga::uint128_t const kcga_ratio = cga::uint128_t ("1000000000000000000000000000"); // 10^27
cga::uint128_t const cga_ratio = cga::uint128_t ("100000000000000000000000"); // 10^23
cga::uint128_t const mcga_ratio = cga::uint128_t ("1000000000000000000000"); // 10^21
cga::uint128_t const ucga_ratio = cga::uint128_t ("1000000000000000000"); // 10^18


union uint128_union
{
public:
	uint128_union () = default;
	/**
	 * Decode from hex string
	 * @warning Aborts at runtime if the input is invalid
	 */
	uint128_union (std::string const &);
	uint128_union (uint64_t);
	uint128_union (cga::uint128_union const &) = default;
	uint128_union (cga::uint128_t const &);
	bool operator== (cga::uint128_union const &) const;
	bool operator!= (cga::uint128_union const &) const;
	bool operator< (cga::uint128_union const &) const;
	bool operator> (cga::uint128_union const &) const;
	void encode_hex (std::string &) const;
	bool decode_hex (std::string const &);
	void encode_dec (std::string &) const;
	bool decode_dec (std::string const &);
	std::string format_balance (cga::uint128_t scale, int precision, bool group_digits);
	std::string format_balance (cga::uint128_t scale, int precision, bool group_digits, const std::locale & locale);
	cga::uint128_t number () const;
	void clear ();
	bool is_zero () const;
	std::string to_string () const;
	std::string to_string_dec () const;
	std::array<uint8_t, 16> bytes;
	std::array<char, 16> chars;
	std::array<uint32_t, 4> dwords;
	std::array<uint64_t, 2> qwords;
};
// Balances are 128 bit.
using amount = uint128_union;
class raw_key;
union uint256_union
{
	uint256_union () = default;
	/**
	 * Decode from hex string
	 * @warning Aborts at runtime if the input is invalid
	 */
	uint256_union (std::string const &);
	uint256_union (uint64_t);
	uint256_union (cga::uint256_t const &);
	void encrypt (cga::raw_key const &, cga::raw_key const &, uint128_union const &);
	uint256_union & operator^= (cga::uint256_union const &);
	uint256_union operator^ (cga::uint256_union const &) const;
	bool operator== (cga::uint256_union const &) const;
	bool operator!= (cga::uint256_union const &) const;
	bool operator< (cga::uint256_union const &) const;
	void encode_hex (std::string &) const;
	bool decode_hex (std::string const &);
	void encode_dec (std::string &) const;
	bool decode_dec (std::string const &);
	void encode_account (std::string &) const;
	std::string to_account () const;
	bool decode_account (std::string const &);
	std::array<uint8_t, 32> bytes;
	std::array<char, 32> chars;
	std::array<uint32_t, 8> dwords;
	std::array<uint64_t, 4> qwords;
	std::array<uint128_union, 2> owords;
	void clear ();
	bool is_zero () const;
	std::string to_string () const;
	cga::uint256_t number () const;
};
// All keys and hashes are 256 bit.
using block_hash = uint256_union;
using account = uint256_union;
using public_key = uint256_union;
using private_key = uint256_union;
using secret_key = uint256_union;
class raw_key
{
public:
	raw_key () = default;
	~raw_key ();
	void decrypt (cga::uint256_union const &, cga::raw_key const &, uint128_union const &);
	bool operator== (cga::raw_key const &) const;
	bool operator!= (cga::raw_key const &) const;
	cga::uint256_union data;
};
union uint512_union
{
	uint512_union () = default;
	uint512_union (cga::uint256_union const &, cga::uint256_union const &);
	uint512_union (cga::uint512_t const &);
	bool operator== (cga::uint512_union const &) const;
	bool operator!= (cga::uint512_union const &) const;
	cga::uint512_union & operator^= (cga::uint512_union const &);
	void encode_hex (std::string &) const;
	bool decode_hex (std::string const &);
	std::array<uint8_t, 64> bytes;
	std::array<uint32_t, 16> dwords;
	std::array<uint64_t, 8> qwords;
	std::array<uint256_union, 2> uint256s;
	void clear ();
	bool is_zero () const;
	cga::uint512_t number () const;
	std::string to_string () const;
};
// Only signatures are 512 bit.
using signature = uint512_union;

cga::uint512_union sign_message (cga::raw_key const &, cga::public_key const &, cga::uint256_union const &);
bool validate_message (cga::public_key const &, cga::uint256_union const &, cga::uint512_union const &);
bool validate_message_batch (const unsigned char **, size_t *, const unsigned char **, const unsigned char **, size_t, int *);
void deterministic_key (cga::uint256_union const &, uint32_t, cga::uint256_union &);
cga::public_key pub_key (cga::private_key const &);
}

namespace std
{
template <>
struct hash<::cga::uint256_union>
{
	size_t operator() (::cga::uint256_union const & data_a) const
	{
		return *reinterpret_cast<size_t const *> (data_a.bytes.data ());
	}
};
template <>
struct hash<::cga::uint256_t>
{
	size_t operator() (::cga::uint256_t const & number_a) const
	{
		return number_a.convert_to<size_t> ();
	}
};
template <>
struct hash<::cga::uint512_union>
{
	size_t operator() (::cga::uint512_union const & data_a) const
	{
		return *reinterpret_cast<size_t const *> (data_a.bytes.data ());
	}
};
}
