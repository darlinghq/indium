#pragma once

#include <utility>

#define INDIUM_PROPERTY_READONLY(_type, _lower, _upper, _restOfName) \
	public: \
		_type _lower ## _restOfName() { return _ ## _lower ## _restOfName; } \
		_type _lower ## _restOfName() const { return _ ## _lower ## _restOfName; } \
	protected: \
		_type _ ## _lower ## _restOfName

#define INDIUM_PROPERTY_REF(_type, _lower, _upper, _restOfName) \
	public: \
		_type& _lower ## _restOfName() { return _ ## _lower ## _restOfName; } \
		_type const& _lower ## _restOfName() const { return _ ## _lower ## _restOfName; } \
	protected: \
		_type _ ## _lower ## _restOfName

#define INDIUM_PROPERTY_READONLY_OBJECT(_type, _lower, _upper, _restOfName) INDIUM_PROPERTY_READONLY(std::shared_ptr<_type>, _lower, _upper, _restOfName)

#define INDIUM_PROPERTY(_type, _lower, _upper, _restOfName) \
		void set ## _upper ## _restOfName(_type value) { _ ## _lower ## _restOfName = value; } \
		INDIUM_PROPERTY_READONLY(_type, _lower, _upper, _restOfName)

#define INDIUM_PROPERTY_OBJECT(_type, _lower, _upper, _restOfName) INDIUM_PROPERTY(std::shared_ptr<_type>, _lower, _upper, _restOfName)

#define INDIUM_PROPERTY_VECTOR(_type, _lower, _upper, _restOfName) \
	public: \
		std::vector<_type>& _lower ## _restOfName() { return _ ## _lower ## _restOfName; } \
		const std::vector<_type>& _lower ## _restOfName() const { return _ ## _lower ## _restOfName; } \
	protected: \
		std::vector<_type> _ ## _lower ## _restOfName

#define INDIUM_PROPERTY_OBJECT_VECTOR(_type, _lower, _upper, _restOfName) INDIUM_PROPERTY_VECTOR(std::shared_ptr<_type>, _lower, _upper, _restOfName)

#define INDIUM_DEFAULT_CONSTRUCTOR(_className) explicit _className(const PreventConstruction&) {}

#define INDIUM_PREVENT_COPY(_className) \
	_className(const _className&) = delete; \
	_className& operator=(const _className&) = delete;

#define INDIUM_BITFLAG_ENUM_CLASS(_enumClass) \
	constexpr _enumClass operator&(const _enumClass& left, const _enumClass& right) { \
		return static_cast<_enumClass>(static_cast<std::underlying_type_t<_enumClass>>(left) & static_cast<std::underlying_type_t<_enumClass>>(right)); \
	}; \
	constexpr _enumClass operator|(const _enumClass& left, const _enumClass& right) { \
		return static_cast<_enumClass>(static_cast<std::underlying_type_t<_enumClass>>(left) | static_cast<std::underlying_type_t<_enumClass>>(right)); \
	}; \
	constexpr bool operator!(const _enumClass& value) { \
		return !static_cast<std::underlying_type_t<_enumClass>>(value); \
	};
