/**
 * bcm2-utils
 * Copyright (C) 2016 Joseph Lehner <joseph.c.lehner@gmail.com>
 *
 * bcm2-utils is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * bcm2-utils is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with bcm2-utils.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef BCM2CFG_NONVOL_H
#define BCM2CFG_NONVOL_H
#include <type_traits>
#include <iostream>
#include <limits>
#include <vector>
#include <memory>
#include <string>
#include <map>
#include "profile.h"
#include "util.h"
#undef minor
#undef major

using bcm2dump::sp;
using bcm2dump::csp;

namespace bcm2cfg {

struct serializable
{
	virtual ~serializable() {}
	virtual std::istream& read(std::istream& is) = 0;
	virtual std::ostream& write(std::ostream& os) const = 0;
};

struct cloneable
{
	virtual ~cloneable() {}
	virtual cloneable* clone() const = 0;
};

template<class T> struct nv_type
{
	static std::string name()
	{
		return T().type();
	}

	static size_t bytes()
	{
		return T().bytes();
	}
};

template<class To, class From, class ToType> sp<To> nv_val_cast(const From& from);

class nv_compound;

class nv_val : public serializable
{
	public:
	struct named
	{
		named(const std::string& name, const sp<nv_val>& val)
		: name(name), val(val) {}

		std::string name;
		sp<nv_val> val;
	};
	typedef std::vector<named> list;

	virtual ~nv_val() {}

	virtual std::string type() const = 0;
	virtual std::string to_string(unsigned level, bool pretty) const = 0;

	virtual std::string to_str() const final
	{ return to_string(0, false); }

	virtual std::string to_pretty(unsigned level = 0) const final
	{ return to_string(level, true); }

	virtual bool parse(const std::string& str) = 0;
	virtual nv_val& parse_checked(const std::string& str) final;

	bool is_set() const
	{ return m_set; }

	// when default-constructed, return the minimum byte count
	// required for this type. after a value has been set, return
	// the number of bytes required to store this value
	virtual size_t bytes() const = 0;

	// for compound types, provide a facility to get/set only a part
	virtual csp<nv_val> get(const std::string& name) const;

	template<class T> csp<T> get_as(const std::string& name) const
	{ return nv_val_cast<const T>(get(name)); }

	virtual void set(const std::string& name, const std::string& val);

	virtual void disable(bool disable)
	{ m_disabled = disable; }
	virtual bool is_disabled() const
	{ return m_disabled; }

	virtual bool is_compound() const
	{ return false; }

	const nv_compound* parent() const
	{ return m_parent; }

	void parent(const nv_compound* parent)
	{ m_parent = parent; }

	friend std::ostream& operator<<(std::ostream& os, const nv_val& val)
	{ return (os << val.to_pretty()); }

	protected:

	bool m_disabled = false;
	bool m_set = false;

	private:
	const nv_compound* m_parent = nullptr;
};

template<class To, class From, class ToType = nv_type<To>> std::shared_ptr<To> nv_val_cast(const std::shared_ptr<From>& from)
{

	sp<To> p = std::dynamic_pointer_cast<To>(from);
	if (!p) {
		throw std::invalid_argument("failed cast: " + from->type() + " (" + from->to_str() + ") -> " +
				ToType::name());
	}

	return p;
}

template<class T, bool BigEndian,
		T Min = std::numeric_limits<T>::min(),
		T Max = std::numeric_limits<T>::max()>
class nv_num : public nv_val
{
	public:
	typedef T num_type;

	static constexpr T min = Min;
	static constexpr T max = Max;

	explicit nv_num(bool hex = false) : m_val(0), m_hex(hex) {}
	nv_num(T val, bool hex) : m_val(val), m_hex(hex) { m_set = true; }

	virtual void hex(bool hex = true)
	{ m_hex = hex; }

	virtual std::string type() const override
	{
		std::string name;

		if (m_hex) {
			name = "x";
		} else if (std::is_signed<T>::value) {
			name = "i";
		} else {
			name = "u";
		}

		name += std::to_string(8 * sizeof(T));

		if (sizeof(T) > 1) {
			name += (BigEndian) ? "be" : "le";
		}

		if (Min != std::numeric_limits<T>::min() || Max != std::numeric_limits<T>::max()) {
			name += "<" + std::to_string(Min) + "," + std::to_string(Max) + ">";
		}

		return name;
	}

	virtual std::string to_string(unsigned, bool pretty) const override
	{
		std::string str;

		if (!m_hex) {
			str = std::to_string(m_val);
		} else {
			str = "0x" + bcm2dump::to_hex(m_val);
		}

		if (pretty && (m_val < Min || m_val > Max)) {
			str += " (out of range)";
		}

		return str;
	}

	virtual bool parse(const std::string& str) override
	{
		try {
			T val = bcm2dump::lexical_cast<T>(str, 0);
			if (val < Min || val > Max) {
				return false;
			}

			m_val = val;
			m_set = true;
			return true;
		} catch (const bcm2dump::bad_lexical_cast& e) {
			return false;
		}
	}

	virtual std::istream& read(std::istream& is) override
	{
		if (read(is, m_val)) {
			m_set = true;
		}

		return is;
	}

	virtual std::ostream& write(std::ostream& os) const override
	{ return write(os, m_val); }

	virtual size_t bytes() const override
	{ return sizeof(T); }

	virtual const T& num() const
	{ return m_val; }

	virtual void num(const T& val)
	{ m_val = val; }

	bool operator!=(const nv_num<T, BigEndian>& other)
	{ return m_val == other.m_val; }

	template<class U> static std::ostream& write(std::ostream& os, U num)
	{
		if (num > max) {
			throw std::invalid_argument("value exceeds maximum of target type");
		}

		T raw = num;

		if (sizeof(raw) > 1) {
			raw = BigEndian ? bcm2dump::h_to_be(raw) : bcm2dump::h_to_le(raw);
		}

		return os.write(reinterpret_cast<const char*>(&raw), sizeof(raw));
	}

	/*
	static std::ostream& write(std::ostream& os, const T& num)
	{
		return write<T>(os, num);
	}
	*/

	template<class U> static std::istream& read(std::istream& in, U& num)
	{
		static_assert(sizeof(U) >= sizeof(T));

		T raw;

		if (in.read(reinterpret_cast<char*>(&raw), sizeof(raw))) {
			if (sizeof(raw) > 1) {
				raw = BigEndian ? bcm2dump::be_to_h(raw) : bcm2dump::le_to_h(raw);
			}

			num = raw;
		}

		return in;
	}

	static std::istream& read(std::istream& in, T& num)
	{
		return read<T>(in, num);
	}

	static T read_num(std::istream& in)
	{
		T num;
		if (!read(in, num)) {
			throw std::runtime_error("failed to read number");
		}
		return num;
	}

	protected:
	T m_val;
	bool m_hex = false;
};

// defines name (unlimited range), name_r (custom range) and name_m (custom maximum)
#define NV_NUM_DEF(name, num_type, be) \
	template<num_type Min = std::numeric_limits<num_type>::min(), num_type Max = std::numeric_limits<num_type>::max()> \
			using name ## _r = nv_num<num_type, be, Min, Max>; \
	template<num_type Max> using name ## _m = name ## _r<std::numeric_limits<num_type>::min(), Max>; \
	typedef name ## _r<> name

// byte types
NV_NUM_DEF(nv_u8, uint8_t, false);
NV_NUM_DEF(nv_i8, int8_t, false);

// big endian types
NV_NUM_DEF(nv_u16, uint16_t, true);
NV_NUM_DEF(nv_u32, uint32_t, true);
NV_NUM_DEF(nv_u64, uint64_t, true);

NV_NUM_DEF(nv_i16, int16_t, true);
NV_NUM_DEF(nv_i32, int32_t, true);
NV_NUM_DEF(nv_i64, int64_t, true);

// little endian types
NV_NUM_DEF(nv_u16le, uint16_t, false);
NV_NUM_DEF(nv_u32le, uint32_t, false);
NV_NUM_DEF(nv_u64le, uint64_t, false);

NV_NUM_DEF(nv_i16le, int16_t, false);
NV_NUM_DEF(nv_i32le, int32_t, false);
NV_NUM_DEF(nv_i64le, int64_t, false);

// TODO split this into nv_compound and nv_compound_base
class nv_compound : public nv_val
{
	public:
	virtual std::string to_string(unsigned level, bool pretty) const override;

	virtual const std::string& name() const
	{ return m_name; }

	virtual void rename(const std::string& name)
	{ m_name = name; }

	virtual bool parse(const std::string& str) override;

	virtual csp<nv_val> get(const std::string& name) const override;
	virtual void set(const std::string& name, const std::string& val) override;
	// like get, but shouldn't throw
	virtual csp<nv_val> find(const std::string& name) const;

	virtual bool init(bool force = false);
	virtual void clear() final
	{ init(true); }

	virtual size_t bytes() const override
	{ return m_bytes ? m_bytes : m_width; }

	virtual std::istream& read(std::istream& is) override;
	virtual std::ostream& write(std::ostream& os) const override;

	virtual bool is_compound() const final
	{ return true; }

	virtual const list& parts() const
	{ return m_parts; }

	protected:
	nv_compound(bool partial, const std::string& name = "")
	: nv_compound(partial, 0, name) {}
	nv_compound(bool partial, size_t width, const std::string& name = "")
	: m_partial(partial), m_width(width), m_name(name) {}
	virtual list definition() const = 0;

	bool m_partial = false;
	// expected final size
	size_t m_width = 0;
	// actual size
	size_t m_bytes = 0;

	list m_parts;

	private:
	std::string m_name;
};

template<> struct nv_type<nv_compound>
{
	static std::string name()
	{ return "nv_compound"; }

	static size_t bytes()
	{ return 0; }
};

template<class From> csp<nv_compound> nv_compound_cast(const std::shared_ptr<From>& from)
{ return nv_val_cast<const nv_compound, From, nv_type<nv_compound>>(from); }

class nv_compound_def : public nv_compound
{
	public:
	nv_compound_def(const std::string& name, const nv_compound::list& def, bool partial = false)
	: nv_compound(partial), m_def(def) { nv_compound::rename(name); }

	virtual std::string type() const override
	{ return name(); }

	protected:
	virtual list definition() const override
	{ return m_def; }

	private:
	nv_compound::list m_def;
};

class nv_array_base : public nv_compound
{
	public:
	// used by to_string to prematurely stop printing elements
	// in a fixed-size list
	typedef std::function<bool(const csp<nv_val>&)> is_end_func;

	virtual std::string to_string(unsigned level, bool pretty) const override;

	protected:
	nv_array_base(size_t width) : nv_compound(false, width), m_is_end(nullptr) {}
	is_end_func m_is_end;
};

template<class T, class I, bool L> class nv_array_generic : public nv_array_base
{
	public:
	nv_array_generic(I n = 0)
	: nv_array_base(n * nv_type<T>::bytes()), m_count(n)
	{
		if (!L && !n) {
			throw std::invalid_argument("size must not be 0");
		}
	}
	virtual ~nv_array_generic() {}

	virtual std::string type() const override
	{
		return std::string(L ? "list" : "array") + "<" + nv_type<T>::name() + ">"
			+ (m_count ? "[" + std::to_string(m_count) + "]" : "");
	}

	virtual std::istream& read(std::istream& is) override
	{
		if (L) {
			if (!m_count && !nv_num<I, true>::read(is, m_count)) {
				return is;
			}
		}

		// FIXME ugly workaround for parsing an array of elements with non-constant width
		size_t min_width = m_width;
		bcm2dump::cleaner c([this, min_width]() { m_width = min_width; });
		if (!L) {
			m_width = 0;
		}

		nv_compound::read(is);

		if (L && !m_count) {
			m_set = true;
		}

		return is;
	}

	virtual std::ostream& write(std::ostream& os) const override
	{
		if (L) {
			if (!nv_num<I, true>::write(os, m_count) || !m_count) {
				return os;
			}
		}

		return nv_compound::write(os);
	}

	virtual void set(const std::string& name, const std::string& val) override
	{
		try {
			I index = bcm2dump::lexical_cast<I>(name);
			if (index < m_count) {
				nv_array_base::set(name, val);
				return;
			}
		} catch (const bcm2dump::bad_lexical_cast& e) {
			// ignored
		}

		if (L && name == "-1") {
			if (m_parts.size() >= std::numeric_limits<I>::max()) {
				throw bcm2dump::user_error("maximum list size reached");
			}
			m_parts.push_back({ std::to_string(m_count), std::make_shared<T>()});
			m_count = m_parts.size();
			nv_array_base::set(std::to_string(m_count - 1), val);
		} else {
			// this will throw an exception
			get(name);
		}
	}

	virtual size_t bytes() const override
	{ return nv_compound::bytes() + (L ? sizeof(I) : 0); }

	protected:

	virtual list definition() const override
	{
		list ret;

		for (I i = 0; i < m_count; ++i) {
			ret.push_back({ std::to_string(i), std::make_shared<T>()});
		}

		return ret;
	}

	private:
	I m_count = 0;
};

template<typename T, size_t N = 0> class nv_array : public nv_array_generic<T, size_t, false>
{
	public:
	typedef std::function<bool(const csp<T>&)> is_end_func;

	// arguments to is_end shall only be of type T, so an unchecked
	// dynamic_cast can be safely used
	nv_array(size_t n = N, const is_end_func& is_end = nullptr)
	: nv_array_generic<T, size_t, false>(n), m_is_end(is_end)
	{
		if (is_end) {
			nv_array_base::m_is_end = [this] (const csp<nv_val>& val) {
				return m_is_end(nv_val_cast<const T>(val));
			};
		}
	}
	virtual ~nv_array() {}

	private:
	is_end_func m_is_end;
};

template<typename T, typename I> using nv_plist = nv_array_generic<T, I, true>;
template<typename T> using nv_p8list = nv_plist<T, uint8_t>;
template<typename T> using nv_p16list = nv_plist<T, uint8_t>;

class nv_data : public nv_val
{
	public:
	explicit nv_data(size_t width);
	virtual std::string type() const override
	{ return "data[" + std::to_string(m_buf.size()) + "]"; }

	virtual std::string to_string(unsigned level, bool pretty) const override;

	virtual bool parse(const std::string& str) override;

	virtual std::istream& read(std::istream& is) override;
	virtual std::ostream& write(std::ostream& os) const override
	{  return os.write(m_buf.data(), m_buf.size()); }

	virtual size_t bytes() const override
	{ return m_buf.size(); }

	virtual csp<nv_val> get(const std::string& name) const override;
	virtual void set(const std::string& name, const std::string& val) override;

	protected:
	std::string m_buf;
};

template<int N> class nv_ip : public nv_data
{
	static_assert(N == 4 || N == 6, "N must be either 4 or 6");
	static constexpr int AF = (N == 4 ? AF_INET : AF_INET6);

	public:
	nv_ip() : nv_data(N == 4 ? 4 : 16) {}

	std::string type() const override
	{ return "ip" + std::to_string(N); }

	std::string to_string(unsigned level, bool pretty) const override
	{
		char addr[32];
		// ugly const_cast because of WINAPI (parameter is marked _In_ only)
		if (inet_ntop(AF, const_cast<char*>(m_buf.data()), addr, sizeof(addr)-1)) {
			return addr;
		}

		return nv_data::to_string(level, pretty);
	}

	bool parse(const std::string& str) override
	{
		// ugly const_cast because of WINAPI (parameter is marked _In_ only)
		return inet_pton(AF, const_cast<char*>(str.c_str()), &m_buf[0]) == 1;
	}
};

class nv_ip4 : public nv_ip<4> {};
class nv_ip6 : public nv_ip<6> {};

class nv_mac : public nv_data
{
	public:
	nv_mac() : nv_data(6) {}

	virtual bool parse(const std::string& str) override;
};

class nv_string : public nv_val
{
	public:
	static constexpr int flag_require_nul = 1;
	static constexpr int flag_optional_nul = 1 << 1;
	static constexpr int flag_is_data = 1 << 2;
	static constexpr int flag_size_includes_prefix = 1 << 3;
	static constexpr int flag_prefix_u8 = 1 << 4;
	static constexpr int flag_prefix_u16 = 1 << 5;
	static constexpr int flag_fixed_width = 1 << 6;

	virtual std::string type() const override;

	virtual bool parse(const std::string& str) override;
	virtual std::string to_string(unsigned level, bool pretty) const override;

	virtual std::istream& read(std::istream& is) override;
	virtual std::ostream& write(std::ostream& os) const override;

	virtual size_t bytes() const override;

	const std::string& str() const { return m_val; }
	void str(const std::string& str) { m_val = str; }

	protected:
	nv_string(int flags, size_t width);

	private:
	int m_flags;
	size_t m_width;
	std::string m_val;
};

namespace detail {
template<int FLAGS, size_t WIDTH = 0> class nv_string_tmpl : public nv_string
{
	public:
	nv_string_tmpl(size_t width = WIDTH) : nv_string(FLAGS, width) {}
};
}

// a fixed-width string, with optional NUL byte (with width 6, "foo" is 66:6f:6f:00:XX:XX, with width 3 it's 66:6f:6f)
template<size_t WIDTH> using nv_fstring = detail::nv_string_tmpl<nv_string::flag_optional_nul, WIDTH>;

// a fixed-width string, with mandatory NUL byte (maximum length is thus WIDTH - 1)
template<size_t WIDTH> using nv_fzstring = detail::nv_string_tmpl<nv_string::flag_require_nul, WIDTH>;

// standard C string
typedef detail::nv_string_tmpl<nv_string::flag_require_nul> nv_zstring;

// u8-prefixed string (u8) with optional NUL terminator ("foo" is 04:66:6f:00 or 03:66:6f:6f)
typedef detail::nv_string_tmpl<nv_string::flag_optional_nul | nv_string::flag_prefix_u8> nv_p8string;

// u8-prefixed string (u8) where the length includes the prefix itself ( "foo" is 04:66:6f:6f)
typedef detail::nv_string_tmpl<nv_string::flag_size_includes_prefix | nv_string::flag_prefix_u8> nv_p8istring;

// u8-prefixed string with mandatory NUL byte ("foo" is 04:66:6f:6f:00)
typedef detail::nv_string_tmpl<nv_string::flag_require_nul | nv_string::flag_prefix_u8> nv_p8zstring;

// u8-prefixed string that is to be interpreted as data
typedef detail::nv_string_tmpl<nv_string::flag_is_data | nv_string::flag_prefix_u8> nv_p8data;

// u16-prefixed string with optional NUL terminator ("foo" is 00:04:66:6f:00 or 00:03:66:6f:6f)
typedef detail::nv_string_tmpl<nv_string::flag_optional_nul | nv_string::flag_prefix_u16> nv_p16string;

// u16-prefixed string (u8) where the length includes the prefix itself ( "foo" is 00:05:66:6f:6f)
typedef detail::nv_string_tmpl<nv_string::flag_size_includes_prefix | nv_string::flag_prefix_u16> nv_p16istring;

// u16-prefixed string with mandatory NUL byte ("foo" is 00:04:66:6f:6f:00)
typedef detail::nv_string_tmpl<nv_string::flag_require_nul | nv_string::flag_prefix_u16> nv_p16zstring;

// u16-prefixed string that is to be interpreted as data
typedef detail::nv_string_tmpl<nv_string::flag_is_data | nv_string::flag_prefix_u16> nv_p16data;

class nv_bool : public nv_u8_m<1>
{
	public:
	virtual std::string type() const override
	{ return "bool"; }

	virtual std::string to_string(unsigned level, bool pretty) const override
	{ return m_val <= 1 ? (m_val ? "yes" : "no") : nv_u8_m<1>::to_string(level, pretty); }

	virtual bool parse(const std::string& str) override;
};

template<typename T, bool B> class nv_enum_bitmask : public T
{
	public:
	typedef typename T::num_type num_type;
	typedef typename std::map<num_type, std::string> valmap;
	typedef std::vector<std::string> valvec;

	virtual ~nv_enum_bitmask() {}

	virtual std::string type() const override
	{
		using bcm2dump::to_hex;

		std::string type = m_name;
		if (m_vec.empty() && m_map.empty()) {
			return type;
		}

		type += " {";

		if (!m_vec.empty()) {
			for (num_type i = 0; i < num_type(m_vec.size()); ++i) {
				if (m_vec[i].empty()) {
					continue;
				}

				type += "\n  " + (B ? "0x" + to_hex(1 << i) : std::to_string(i)) + " = " + m_vec[i];
			}

		} else if (!m_map.empty()) {
			for (auto v : m_map) {
				if (v.second.empty()) {
					continue;
				}

				type += "\n  " + (B ? "0x" + to_hex(v.first) : std::to_string(v.first)) + " = " + v.second;
			}
		}

		return type + "\n}";
	}

	protected:
	nv_enum_bitmask(const std::string& name, const valvec& vals) : nv_enum_bitmask(name, vals.size()) { m_vec = vals; }
	nv_enum_bitmask(const std::string& name, const valmap& vals) : nv_enum_bitmask(name, vals.size()) { m_map = vals; }
	nv_enum_bitmask(const std::string& name) : nv_enum_bitmask(name, 0) {}

	bool str_to_num(const std::string& str, num_type& num, bool bitmask) const
	{
		for (num_type i = 0; i < num_type(m_vec.size()); ++i) {
			if (m_vec[i] == str) {
				num = bitmask ? 1 << i : i;
				return true;
			}
		}

		for (auto v : m_map) {
			if (v.second == str) {
				num = v.first;
				return true;
			}
		}

		try {
			num = bcm2dump::lexical_cast<num_type>(str, 0);
			return true;
		} catch (const bcm2dump::bad_lexical_cast& e) {}

		return false;
	}

	std::string num_to_str(const num_type& num, bool bitmask, bool pretty) const
	{
		std::string str;

		if (!m_map.empty()) {
			auto i = m_map.find(bitmask ? (1 << num) : num);
			if (i != m_map.end()) {
				str = i->second;
			}
		} else if (!m_vec.empty() && num < num_type(m_vec.size())) {
			str = m_vec[num];
		}

		return str;
	}

	const std::string m_name;

	private:
	nv_enum_bitmask(const std::string& name, size_t n)
	: m_name(!name.empty() ? name : (B ? "bitmask" : "enum"))
	{
		if (n > std::numeric_limits<num_type>::max()) {
			throw std::invalid_argument("number of enum elements exceeds maximum for " + nv_type<T>::name());
		}
	}

	valmap m_map;
	valvec m_vec;

};

template<class T> class nv_enum : public nv_enum_bitmask<T, false>
{
	protected:
	typedef nv_enum_bitmask<T, false> super;

	public:
	nv_enum()
	: super("") {}

	nv_enum(const std::string& name, const typename super::valmap& vals)
	: super(name, vals) {}
	nv_enum(const std::string& name, const typename super::valvec& vals)
	: super(name, vals) {}

	virtual ~nv_enum() {}

	virtual std::string to_string(unsigned, bool pretty) const override
	{
		std::string str = super::num_to_str(T::m_val, false, pretty);
		return !str.empty() ? str : super::m_name + "(" + std::to_string(super::num()) + ")";
	}

	virtual bool parse(const std::string& str) override
	{
		if(super::str_to_num(str, T::m_val, false)) {
			T::m_set = true;
			return true;
		}

		return false;
	}
};

template<class T> class nv_bitmask : public nv_enum_bitmask<T, true>
{
	typedef nv_enum_bitmask<T, true> super;
	typedef typename super::num_type num_type;

	public:
	nv_bitmask(const std::string& name = "")
	: super(!name.empty() ? name : "bitmask") {}
	nv_bitmask(const typename super::valmap& vals)
	: super("", vals) {}
	nv_bitmask(const typename super::valvec& vals)
	: super("", vals) {}
	nv_bitmask(const std::string& name, const typename super::valmap& vals)
	: super(name, vals) {}
	nv_bitmask(const std::string& name, const typename super::valvec& vals)
	: super(name, vals) {}

	virtual ~nv_bitmask() {}

	virtual std::string to_string(unsigned, bool pretty) const override
	{
		if (T::m_val == 0) {
			return "0x" + bcm2dump::to_hex(T::m_val);
		}

		std::string ret;

		for (size_t i = 0; i != sizeof(num_type) * 8; ++i) {
			num_type flag = 1 << i;
			if (T::m_val & flag) {
				if (!ret.empty()) {
					ret += pretty ? " | " : "|";
				}
				std::string str = super::num_to_str(i, true, pretty);
				ret += str.empty() ? "0x" + bcm2dump::to_hex(flag) : str;
			}
		}

		return ret;
	}

	virtual bool parse(const std::string& str) override
	{
		if (!str.empty()) {
			num_type n;
			if (str[0] == '+' || str[0] == '-') {
				if (!super::str_to_num(str.substr(1), n, true)) {
					return false;
				}

				if (str[0] == '+') {
					T::m_val |= n;
				} else {
					T::m_val &= ~n;
				}
			} else if(super::str_to_num(str, n, true)) {
				T::m_val = n;
			} else {
				return false;
			}

			T::m_set = true;
			return true;
		}

		return false;
	}
};


class nv_magic : public nv_data
{
	public:
	nv_magic() : nv_data(4) {}
	nv_magic(const std::string& magic);
	nv_magic(uint32_t magic);

	virtual std::string type() const override
	{ return "magic"; }

	virtual bool parse(const std::string& str) override;

	virtual std::string to_string(unsigned, bool pretty) const override;

	const std::string& raw() const
	{ return m_buf; }

	bool operator<(const nv_magic& other) const
	{ return m_buf < other.m_buf; }

	bool operator==(const nv_magic& other) const
	{ return m_buf == other.m_buf; }

	bool operator!=(const nv_magic& other) const
	{ return !(*this == other); }

};

class nv_version : public nv_u16
{
	public:
	nv_version() {}
	nv_version(uint8_t maj, uint8_t min)
	{ m_val = maj << 8 | min; }

	virtual std::string type() const override
	{ return "version"; }

	virtual std::string to_string(unsigned, bool) const override
	{ return std::to_string(m_val >> 8) + "." + std::to_string(m_val & 0xff); }

	bool operator==(const nv_version& other)
	{ return m_val == other.m_val; }

	bool operator<(const nv_version& other)
	{ return m_val < other.m_val; }

	uint8_t major() const
	{ return m_val >> 8; }

	uint8_t minor() const
	{ return m_val & 0xff; }
};

class nv_group : public nv_compound, public cloneable
{
	public:
	static constexpr int fmt_unknown = 0;
	static constexpr int fmt_perm = 1;
	static constexpr int fmt_dyn = 2;
	static constexpr int fmt_gws = 3;
	static constexpr int fmt_gwsdyn = 4;
	static constexpr int fmt_boltenv = 5;

	nv_group(uint32_t magic, const std::string& name = "")
	: nv_group(nv_magic(magic), name) {}

	nv_group(const std::string& magic, const std::string& name = "")
	: nv_group(nv_magic(magic), name) {}

	nv_group(const nv_magic& magic, const std::string& name);

	virtual bool is_versioned() const
	{ return true; }

	virtual std::string type() const override
	{ return "group[" + m_magic.to_str() + "]"; }

	virtual std::ostream& write(std::ostream& os) const override;

	static std::istream& read(std::istream& is, sp<nv_group>& group, int format,
			size_t remaining, const csp<bcm2dump::profile>& profile);
	static void registry_add(const csp<nv_group>& group);

	virtual nv_group* clone() const override = 0;

	virtual const nv_magic& magic() const
	{ return m_magic; }

	virtual const nv_version& version() const
	{ return m_version; }

	bool init(bool force) override;

	csp<bcm2dump::profile> profile() const
	{ return m_profile; }

	protected:
	static std::map<nv_magic, csp<nv_group>>& registry();

	virtual list definition() const override final;
	virtual list definition(int format, const nv_version& ver) const;
	virtual std::istream& read(std::istream& is) override;

	uint16_t size() const
	{ return m_size.num(); }

	nv_u16 m_size;
	nv_magic m_magic;
	nv_version m_version;
	int m_format = fmt_unknown;
	csp<bcm2dump::profile> m_profile;

	private:
	static std::map<nv_magic, csp<nv_group>> s_registry;

};

template<> struct nv_type<nv_group>
{
	static std::string name()
	{ return "nv_group"; }

	static size_t bytes()
	{ return 0; }
};

class nv_group_generic : public nv_group
{
	public:
	using nv_group::nv_group;

	virtual nv_group_generic* clone() const override
	{ return new nv_group_generic(*this); }
};
}

template struct bcm2dump_def_comparison_operators<bcm2cfg::nv_version>;

#endif
