#pragma once

#include <cstdint>
#include <type_traits>
#include <stdexcept>
#include <string_view>
#include <cstring>

namespace Iridium {
	enum Endianness {
		Little = 0,
		Big = 1,
	};

#if (defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__) || defined(__BIG_ENDIAN__)
	constexpr Endianness nativeEndianness = Endianness::Big;
#else
	constexpr Endianness nativeEndianness = Endianness::Little;
#endif

	template<typename T>
	std::make_unsigned_t<T> byteswapInteger(std::make_unsigned_t<T> integer);

	template<typename T, Endianness endianness>
	T readInteger(const void* data);

	template<typename T, Endianness endianness>
	void writeInteger(void* data, T integer);

	template<typename T>
	T readIntegerLE(const void* data) {
		return readInteger<T, Endianness::Little>(data);
	};

	template<typename T>
	T readIntegerBE(const void* data) {
		return readInteger<T, Endianness::Big>(data);
	};

	template<bool constant>
	class ByteView {
	public:
		using CharType = std::conditional_t<constant, const char, char>;
		using VoidType = std::conditional_t<constant, const void, void>;

	protected:
		CharType* _data;
		size_t _size;
		size_t _offset;

	public:
		ByteView(VoidType* data, size_t size, size_t offset = 0):
			_data(static_cast<CharType*>(data)),
			_size(size),
			_offset(offset)
			{};

		void seek(size_t offset) {
			if (offset > _size) {
				throw std::out_of_range("Offset out of range of data");
			}
			_offset = offset;
		};

		void skip(size_t count) {
			if (count > _size - _offset) {
				throw std::out_of_range("Skipping more bytes than are available");
			}
			_offset += count;
		};

		size_t offset() const {
			return _offset;
		};

		size_t size() const {
			return _size;
		};

		size_t remainingSize() const {
			return _size - _offset;
		};

		const void* data() const {
			return _data;
		};

		void resize(size_t newSize) {
			_size = newSize;

			if (_offset > _size) {
				_offset = _size;
			}
		};
	};

	class ByteReader: public ByteView<true> {
	public:
		ByteReader(const void* data, size_t size, size_t offset = 0):
			ByteView(data, size, offset)
			{};

		template<typename T, Endianness endianness>
		T readInteger() {
			if (sizeof(T) > _size - _offset) {
				throw std::out_of_range("Not enough space for integer");
			}

			auto result = Iridium::readInteger<T, endianness>(static_cast<const void*>(_data + _offset));
			_offset += sizeof(T);
			return result;
		};

		template<typename T>
		T readIntegerLE() {
			return readInteger<T, Endianness::Little>();
		};

		template<typename T>
		T readIntegerBE() {
			return readInteger<T, Endianness::Big>();
		};

		std::string_view readString(size_t length) {
			if (length > _size - _offset) {
				throw std::out_of_range("Not enough space for string");
			}

			std::string_view result(_data + _offset, length);
			_offset += length;
			return result;
		};

		std::string_view readVariableString() {
			auto length = strnlen(_data + _offset, _size - _offset);
			std::string_view result(_data + _offset, length);
			_offset += length + ((length == _size - _offset) ? 0 : 1);
			return result;
		};

		ByteReader subrange(size_t length) {
			if (length > _size - _offset) {
				throw std::out_of_range("Not enough space for subrange");
			}

			auto reader = ByteReader(_data + _offset, length);
			_offset += length;
			return reader;
		};
	};

	class ByteWriter: public ByteView<false> {
	public:
		ByteWriter(void* data, size_t size, size_t offset = 0):
			ByteView(data, size, offset)
			{};

		template<typename T, Endianness endianness>
		size_t writeInteger(T integer) {
			if (sizeof(T) > _size - _offset) {
				throw std::out_of_range("Not enough space for integer");
			}

			Iridium::writeInteger<T, endianness>(static_cast<void*>(_data + _offset), integer);
			_offset += sizeof(T);
			return sizeof(T);
		};

		template<typename T>
		size_t writeIntegerLE(T integer) {
			return writeInteger<T, Endianness::Little>(integer);
		};

		template<typename T>
		size_t writeIntegerBE(T integer) {
			return writeInteger<T, Endianness::Big>(integer);
		};

		size_t writeString(std::string_view string, bool nullTerminate = false) {
			if (string.size() + (nullTerminate ? 1 : 0) > _size - _offset) {
				throw std::out_of_range("Not enough space for string");
			}

			auto copied = string.copy(_data + _offset, string.size());
			if (nullTerminate) {
				_data[_offset + copied] = '\0';
				++copied;
			}
			_offset += copied;
			return copied;
		};

		ByteWriter subrange(size_t length) {
			if (length > _size - _offset) {
				throw std::out_of_range("Not enough space for subrange");
			}

			auto writer = ByteWriter(_data + _offset, length);
			_offset += length;
			return writer;
		};

		size_t zero(size_t length) {
			if (length > _size - _offset) {
				throw std::out_of_range("Not enough space for zero span");
			}

			memset(_data + _offset, 0, length);
			_offset += length;
			return length;
		};

		size_t writeRaw(const void* data, size_t length) {
			if (length > _size - _offset) {
				throw std::out_of_range("Not enough space for raw data");
			}

			memcpy(_data + _offset, data, length);
			_offset += length;
			return length;
		};

		size_t zeroToAlign(size_t alignment) {
			if ((_offset % alignment) == 0) {
				return 0;
			}

			size_t nextBoundary = _offset + (alignment - (_offset % alignment));

			if (nextBoundary > _size) {
				throw std::out_of_range("Not enough space to align");
			}

			auto count = nextBoundary - _offset;
			memset(_data + _offset, 0, count);
			_offset += count;
			return count;
		};
	};

	class DynamicByteWriter: public ByteWriter {
	private:
		size_t _allocationSize = 0;

		void resizeToFitWrite(size_t writeSize) {
			auto remSize = _size - _offset;

			if (writeSize > remSize) {
				auto extraSize = writeSize - remSize;
				auto remSpace = _allocationSize - _size;

				if (remSpace < extraSize) {
					// we need to reallocate the buffer
					auto minSize = _allocationSize + (extraSize - remSpace);
					// TODO: maybe round this up to a power of 2
					auto newSize = minSize;

					auto tmp = realloc(_data, newSize);
					if (!tmp) {
						throw std::bad_alloc();
					}

					_data = static_cast<char*>(tmp);
					_allocationSize = newSize;
				}

				_size += extraSize;
			}
		};

	public:
		DynamicByteWriter():
			ByteWriter(nullptr, 0)
			{};
		DynamicByteWriter(DynamicByteWriter&& other):
			ByteWriter(other._data, other._size, other._offset),
			_allocationSize(other._allocationSize)
		{
			other._data = nullptr;
			other._size = 0;
			other._offset = 0;
			other._allocationSize = 0;
		};
		DynamicByteWriter(const DynamicByteWriter&) = delete;
		~DynamicByteWriter() {
			if (_data) {
				free(_data);
			}
		};

		DynamicByteWriter& operator=(DynamicByteWriter&& other) {
			if (_data) {
				free(_data);
			}

			_data = other._data;
			_size = other._size;
			_offset = other._offset;
			_allocationSize = other._allocationSize;

			other._data = nullptr;
			other._size = 0;
			other._offset = 0;
			other._allocationSize = 0;

			return *this;
		};
		DynamicByteWriter& operator=(const DynamicByteWriter& other) = delete;

		template<typename T, Endianness endianness>
		size_t writeInteger(T integer) {
			resizeToFitWrite(sizeof(T));
			return ByteWriter::writeInteger<T, endianness>(integer);
		};

		template<typename T>
		size_t writeIntegerLE(T integer) {
			return writeInteger<T, Endianness::Little>(integer);
		};

		template<typename T>
		size_t writeIntegerBE(T integer) {
			return writeInteger<T, Endianness::Big>(integer);
		};

		size_t writeString(std::string_view string, bool nullTerminate = false) {
			resizeToFitWrite(string.size() + (nullTerminate ? 1 : 0));
			return ByteWriter::writeString(string, nullTerminate);
		};

		ByteWriter subrange(size_t length) {
			resizeToFitWrite(length);
			return ByteWriter::subrange(length);
		};

		size_t zero(size_t length) {
			resizeToFitWrite(length);
			return ByteWriter::zero(length);
		};

		void* extractBuffer() {
			auto buf = _data;
			_data = nullptr;
			_size = 0;
			_offset = 0;
			_allocationSize = 0;
			return buf;
		};

		size_t writeRaw(const void* data, size_t length) {
			resizeToFitWrite(length);
			return ByteWriter::writeRaw(data, length);
		};

		void resizeAndSkip(size_t count) {
			resizeToFitWrite(count);
			ByteView::skip(count);
		};

		size_t zeroToAlign(size_t alignment) {
			if ((_offset % alignment) == 0) {
				return 0;
			}

			size_t nextBoundary = _offset + (alignment - (_offset % alignment));
			auto count = nextBoundary - _offset;
			resizeToFitWrite(count);
			memset(_data + _offset, 0, count);
			_offset += count;
			return count;
		};
	};

	template<typename T>
	T roundUpPowerOf2(T integer, T powerOf2) {
		static_assert(std::is_integral_v<T>, "T must be an integral type");
		return (integer + (powerOf2 - 1)) & ~(powerOf2 - 1);
	};

#ifdef __clang__
	using Float16 = __fp16;
#else
	using Float16 = _Float16;
#endif
};

template<typename T>
std::make_unsigned_t<T> byteswapInteger(std::make_unsigned_t<T> integer) {
	static_assert(std::is_integral_v<T>, "T must be an integral type");

	if constexpr (sizeof(T) == 2) {
		integer =
			(integer & 0x00ff) << 8 |
			(integer & 0xff00) >> 8 ;
	} else if constexpr (sizeof(T) == 4) {
		integer =
			(integer & 0x000000ff) << 24 |
			(integer & 0x0000ff00) <<  8 |
			(integer & 0x00ff0000) >>  8 |
			(integer & 0xff000000) >> 24 ;
	} else if constexpr (sizeof(T) == 8) {
		integer =
			(integer & 0x00000000000000ff) << 56 |
			(integer & 0x000000000000ff00) << 40 |
			(integer & 0x0000000000ff0000) << 24 |
			(integer & 0x00000000ff000000) <<  8 |
			(integer & 0x000000ff00000000) >>  8 |
			(integer & 0x0000ff0000000000) >> 24 |
			(integer & 0x00ff000000000000) >> 40 |
			(integer & 0xff00000000000000) >> 56 ;
	}

	return integer;
};

template<typename T, Iridium::Endianness endianness>
T Iridium::readInteger(const void* data) {
	static_assert(std::is_integral_v<T>, "T must be an integral type");

	std::make_unsigned_t<T> tmp = *reinterpret_cast<const T*>(data);

	// check if we need to byteswap
	if constexpr (endianness != Iridium::nativeEndianness) {
		tmp = byteswapInteger(tmp);
	}

	// casting unsigned to signed is implementation-defined, but it works as expected for our target platforms
	return static_cast<T>(tmp);
};

template<typename T, Iridium::Endianness endianness>
void Iridium::writeInteger(void* data, T integer) {
	static_assert(std::is_integral_v<T>, "T must be an integral type");

	// casting signed to unsigned is perfectly valid
	auto tmp = static_cast<std::make_unsigned_t<T>>(integer);

	// check if we need to byteswap
	if constexpr (endianness != Iridium::nativeEndianness) {
		tmp = byteswapInteger(tmp);
	}

	*reinterpret_cast<std::make_unsigned_t<T>*>(data) = tmp;
};
