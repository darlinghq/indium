#include <iridium/mslc-reflection.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <map>
#include <memory>
#include <stdexcept>
#include <vector>

// A JSON reader just big enough for mslc's reflection document: objects, arrays, strings
// without \u escapes beyond the BMP, numbers, booleans and null. It keeps this file free of
// a JSON dependency and of LLVM.
namespace {
	struct Value {
		enum class Kind { Null, Bool, Number, String, Array, Object } kind = Kind::Null;
		bool boolean = false;
		double number = 0;
		std::string string;
		std::vector<Value> array;
		std::map<std::string, Value> object;
	};

	class Parser {
		const char* _p;
		const char* _end;

		[[noreturn]] void fail(const std::string& what) const {
			throw std::runtime_error("invalid reflection JSON: " + what);
		}

		void skipSpace() {
			while (_p < _end && (*_p == ' ' || *_p == '\t' || *_p == '\n' || *_p == '\r')) {
				++_p;
			}
		}

		void expect(char c) {
			skipSpace();
			if (_p >= _end || *_p != c) {
				fail(std::string("expected '") + c + "'");
			}
			++_p;
		}

		std::string parseString() {
			expect('"');
			std::string out;
			while (true) {
				if (_p >= _end) {
					fail("unterminated string");
				}
				char c = *_p++;
				if (c == '"') {
					return out;
				}
				if (c != '\\') {
					out.push_back(c);
					continue;
				}
				if (_p >= _end) {
					fail("unterminated escape");
				}
				c = *_p++;
				switch (c) {
					case '"': case '\\': case '/': out.push_back(c); break;
					case 'b': out.push_back('\b'); break;
					case 'f': out.push_back('\f'); break;
					case 'n': out.push_back('\n'); break;
					case 'r': out.push_back('\r'); break;
					case 't': out.push_back('\t'); break;
					case 'u': {
						if (_end - _p < 4) {
							fail("short \\u escape");
						}
						unsigned code = 0;
						for (int i = 0; i < 4; ++i) {
							char h = *_p++;
							code <<= 4;
							if (h >= '0' && h <= '9') { code |= h - '0'; }
							else if (h >= 'a' && h <= 'f') { code |= h - 'a' + 10; }
							else if (h >= 'A' && h <= 'F') { code |= h - 'A' + 10; }
							else { fail("bad \\u escape"); }
						}
						if (code >= 0xd800 && code <= 0xdfff) {
							fail("surrogate \\u escapes are not supported");
						}
						if (code < 0x80) {
							out.push_back(static_cast<char>(code));
						} else if (code < 0x800) {
							out.push_back(static_cast<char>(0xc0 | (code >> 6)));
							out.push_back(static_cast<char>(0x80 | (code & 0x3f)));
						} else {
							out.push_back(static_cast<char>(0xe0 | (code >> 12)));
							out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3f)));
							out.push_back(static_cast<char>(0x80 | (code & 0x3f)));
						}
						break;
					}
					default: fail("bad escape");
				}
			}
		}

		bool literal(const char* word) {
			size_t n = std::char_traits<char>::length(word);
			if (static_cast<size_t>(_end - _p) >= n && std::char_traits<char>::compare(_p, word, n) == 0) {
				_p += n;
				return true;
			}
			return false;
		}

	public:
		Parser(const char* data, size_t length): _p(data), _end(data + length) {}

		Value parseValue(int depth = 0) {
			if (depth > 32) {
				fail("nesting too deep");
			}
			skipSpace();
			if (_p >= _end) {
				fail("unexpected end");
			}
			Value v;
			if (*_p == '{') {
				++_p;
				v.kind = Value::Kind::Object;
				skipSpace();
				if (_p < _end && *_p == '}') {
					++_p;
					return v;
				}
				while (true) {
					skipSpace();
					std::string key = parseString();
					expect(':');
					v.object[key] = parseValue(depth + 1);
					skipSpace();
					if (_p < _end && *_p == ',') {
						++_p;
						continue;
					}
					expect('}');
					return v;
				}
			}
			if (*_p == '[') {
				++_p;
				v.kind = Value::Kind::Array;
				skipSpace();
				if (_p < _end && *_p == ']') {
					++_p;
					return v;
				}
				while (true) {
					v.array.push_back(parseValue(depth + 1));
					skipSpace();
					if (_p < _end && *_p == ',') {
						++_p;
						continue;
					}
					expect(']');
					return v;
				}
			}
			if (*_p == '"') {
				v.kind = Value::Kind::String;
				v.string = parseString();
				return v;
			}
			if (literal("true")) { v.kind = Value::Kind::Bool; v.boolean = true; return v; }
			if (literal("false")) { v.kind = Value::Kind::Bool; return v; }
			if (literal("null")) { return v; }
			char* numEnd = nullptr;
			std::string text(_p, std::min<size_t>(_end - _p, 64));
			v.number = std::strtod(text.c_str(), &numEnd);
			if (numEnd == text.c_str()) {
				fail("unexpected character");
			}
			_p += numEnd - text.c_str();
			v.kind = Value::Kind::Number;
			return v;
		}

		Value parseDocument() {
			Value v = parseValue();
			skipSpace();
			if (_p != _end) {
				fail("trailing data");
			}
			return v;
		}
	};

	[[noreturn]] void reject(const std::string& where, const std::string& what) {
		throw std::runtime_error("mslc reflection " + where + ": " + what);
	}

	const Value& member(const Value& object, const char* key, Value::Kind kind, const std::string& where) {
		if (object.kind != Value::Kind::Object) {
			reject(where, "not an object");
		}
		auto it = object.object.find(key);
		if (it == object.object.end() || it->second.kind != kind) {
			reject(where, std::string("missing or mistyped \"") + key + "\"");
		}
		return it->second;
	}

	bool has(const Value& object, const char* key) {
		return object.object.find(key) != object.object.end();
	}

	size_t index(const Value& object, const char* key, const std::string& where) {
		double n = member(object, key, Value::Kind::Number, where).number;
		if (n < 0 || n != std::floor(n) || n > 4294967295.0) {
			reject(where, std::string("\"") + key + "\" is not a non-negative integer");
		}
		return static_cast<size_t>(n);
	}

	template<typename T>
	T named(const Value& object, const char* key, const std::map<std::string, T>& names, const std::string& where) {
		const std::string& text = member(object, key, Value::Kind::String, where).string;
		auto it = names.find(text);
		if (it == names.end()) {
			reject(where, std::string("unknown \"") + key + "\" value \"" + text + "\"");
		}
		return it->second;
	}

	using ES = Iridium::EmbeddedSampler;

	Iridium::EmbeddedSampler readEmbeddedSampler(const Value& v, const std::string& where) {
		static const std::map<std::string, ES::AddressMode> addresses {
			{ "ClampToZero", ES::AddressMode::ClampToZero }, { "ClampToEdge", ES::AddressMode::ClampToEdge },
			{ "Repeat", ES::AddressMode::Repeat }, { "MirrorRepeat", ES::AddressMode::MirrorRepeat },
			{ "ClampToBorderColor", ES::AddressMode::ClampToBorderColor },
		};
		static const std::map<std::string, ES::Filter> filters {
			{ "Nearest", ES::Filter::Nearest }, { "Linear", ES::Filter::Linear },
		};
		static const std::map<std::string, ES::MipFilter> mipFilters {
			{ "None", ES::MipFilter::None }, { "Nearest", ES::MipFilter::Nearest }, { "Linear", ES::MipFilter::Linear },
		};
		static const std::map<std::string, ES::CompareFunction> compares {
			{ "Never", ES::CompareFunction::Never }, { "Less", ES::CompareFunction::Less },
			{ "LessEqual", ES::CompareFunction::LessEqual }, { "Greater", ES::CompareFunction::Greater },
			{ "GreaterEqual", ES::CompareFunction::GreaterEqual }, { "Equal", ES::CompareFunction::Equal },
			{ "NotEqual", ES::CompareFunction::NotEqual }, { "Always", ES::CompareFunction::Always },
		};
		static const std::map<std::string, ES::BorderColor> borders {
			{ "TransparentBlack", ES::BorderColor::TransparentBlack }, { "OpaqueBlack", ES::BorderColor::OpaqueBlack },
			{ "OpaqueWhite", ES::BorderColor::OpaqueWhite },
		};

		ES s {};
		s.widthAddressMode = named(v, "s_address", addresses, where);
		s.heightAddressMode = named(v, "t_address", addresses, where);
		s.depthAddressMode = named(v, "r_address", addresses, where);
		s.magnificationFilter = named(v, "mag_filter", filters, where);
		s.minificationFilter = named(v, "min_filter", filters, where);
		s.mipmapFilter = named(v, "mip_filter", mipFilters, where);
		s.usesNormalizedCoordinates = member(v, "normalized_coordinates", Value::Kind::Bool, where).boolean;
		s.compareFunction = named(v, "compare_function", compares, where);
		size_t anisotropy = index(v, "anisotropy", where);
		if (anisotropy > 255) {
			reject(where, "\"anisotropy\" does not fit in a byte");
		}
		s.anisotropyLevel = static_cast<uint8_t>(anisotropy);
		s.borderColor = named(v, "border_color", borders, where);
		s.lodMin = static_cast<float>(member(v, "lod_min", Value::Kind::Number, where).number);
		s.lodMax = static_cast<float>(member(v, "lod_max", Value::Kind::Number, where).number);
		return s;
	}

	// The descriptor's binding. The set is checked against what indium builds: one layout per
	// stage, which mslc numbers 0 for vertex and compute and 1 for fragment.
	size_t descriptorBinding(const Value& entry, Iridium::FunctionType stage, const std::string& where) {
		const Value& descriptor = member(entry, "descriptor", Value::Kind::Object, where);
		size_t set = index(descriptor, "set", where + " descriptor");
		size_t expected = stage == Iridium::FunctionType::Fragment ? 1 : 0;
		if (set != expected) {
			reject(where, "descriptor set " + std::to_string(set) + " is not the " + std::to_string(expected) + " that indium builds for this stage");
		}
		return index(descriptor, "binding", where + " descriptor");
	}

	Iridium::FunctionInfo readEntryPoint(const Value& entry, const std::string& where) {
		static const std::map<std::string, Iridium::FunctionType> stages {
			{ "vertex", Iridium::FunctionType::Vertex }, { "fragment", Iridium::FunctionType::Fragment },
			{ "compute", Iridium::FunctionType::Kernel },
		};
		static const std::map<std::string, Iridium::TextureAccessType> accesses {
			{ "Sample", Iridium::TextureAccessType::Sample }, { "Read", Iridium::TextureAccessType::Read },
			{ "Write", Iridium::TextureAccessType::Write }, { "ReadWrite", Iridium::TextureAccessType::ReadWrite },
		};

		Iridium::FunctionInfo info;
		info.type = named(entry, "stage", stages, where);

		for (const Value& sampler: member(entry, "embedded_samplers", Value::Kind::Array, where).array) {
			info.embeddedSamplers.push_back(readEmbeddedSampler(sampler, where + " embedded sampler"));
		}

		size_t position = 0;
		for (const Value& binding: member(entry, "bindings", Value::Kind::Array, where).array) {
			std::string at = where + " binding " + std::to_string(position++);
			const std::string& kind = member(binding, "kind", Value::Kind::String, at).string;
			using Iridium::BindingType;
			using Iridium::BindingInfo;

			if (kind == "Buffer") {
				// The buffers share one address block at binding 0; indium keys them by Metal index.
				info.bindings.push_back(BindingInfo { BindingType::Buffer, index(binding, "metal_index", at), 0 });
			} else if (kind == "Texture") {
				info.bindings.push_back(BindingInfo { BindingType::Texture, index(binding, "metal_index", at),
					descriptorBinding(binding, info.type, at), named(binding, "texture_access", accesses, at) });
			} else if (kind == "Sampler") {
				size_t internal = descriptorBinding(binding, info.type, at);
				if (has(binding, "embedded_sampler")) {
					size_t which = index(binding, "embedded_sampler", at);
					if (which >= info.embeddedSamplers.size()) {
						reject(at, "embedded_sampler " + std::to_string(which) + " is out of range");
					}
					info.bindings.push_back(BindingInfo { BindingType::Sampler, SIZE_MAX, internal, Iridium::TextureAccessType::Read, which });
				} else {
					info.bindings.push_back(BindingInfo { BindingType::Sampler, index(binding, "metal_index", at), internal, Iridium::TextureAccessType::Read, SIZE_MAX });
				}
			} else if (kind == "VertexInput") {
				info.bindings.push_back(BindingInfo { BindingType::VertexInput, index(binding, "metal_index", at), 0, Iridium::TextureAccessType::Read, 0 });
			} else if (kind == "InputAttachment") {
				if (info.type != Iridium::FunctionType::Fragment) {
					reject(at, "an input attachment outside a fragment function");
				}
				// metal_index and input_attachment_index are both the colour attachment index; indium
				// puts that colour attachment at pInputAttachments[index] and the view at the binding.
				size_t color = index(binding, "metal_index", at);
				if (index(binding, "input_attachment_index", at) != color) {
					reject(at, "metal_index and input_attachment_index differ");
				}
				info.bindings.push_back(BindingInfo { BindingType::InputAttachment, color, descriptorBinding(binding, info.type, at) });
			} else if (kind == "TextureBuffer" || kind == "ThreadgroupMemory") {
				reject(at, "kind \"" + kind + "\" is not supported by indium");
			}
			// Any other kind is one this reader predates: mslc's contract is to skip it.
		}

		return info;
	}
}

bool Iridium::parseMslcReflection(const char* json, size_t length, OutputInfo& outputInfo, std::string& error) {
	try {
		Value root = Parser(json, length).parseDocument();
		if (index(root, "reflection_version", "document") != 2) {
			reject("document", "reflection_version is not 2");
		}
		OutputInfo result;
		size_t position = 0;
		for (const Value& entry: member(root, "entry_points", Value::Kind::Array, "document").array) {
			std::string where = "entry point " + std::to_string(position++);
			std::string name = member(entry, "name", Value::Kind::String, where).string;
			if (!result.functionInfos.emplace(name, readEntryPoint(entry, "entry point \"" + name + "\"")).second) {
				reject(where, "duplicate entry point name \"" + name + "\"");
			}
		}
		outputInfo = std::move(result);
		return true;
	} catch (const std::exception& e) {
		error = e.what();
		return false;
	}
}
