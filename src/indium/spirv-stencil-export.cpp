#include <indium/library.private.hpp>

#include <unordered_map>

namespace {
	constexpr size_t headerWords = 5;
	constexpr uint32_t version1_4 = 0x00010400;

	constexpr uint32_t opExtension = 10, opEntryPoint = 15, opExecutionMode = 16, opCapability = 17, opTypePointer = 32,
		opFunction = 54, opVariable = 59, opLoad = 61, opStore = 62, opCopyMemory = 63, opCopyMemorySized = 64,
		opDecorate = 71, opMemberDecorate = 72;
	constexpr uint32_t capabilityStencilExport = 5013, decorationBuiltIn = 11, builtInFragStencilRef = 5014,
		executionModeStencilRefReplacing = 5027, storageClassOutput = 3, storageClassPrivate = 6;

	// The literal string operand that starts at `first`, and where the words after it start.
	std::string literalString(const uint32_t* words, size_t first, size_t limit, size_t& end) {
		std::string text;
		size_t at = first;
		for (; at < limit; ++at) {
			bool terminated = false;
			for (int byte = 0; byte < 4; ++byte) {
				char c = static_cast<char>((words[at] >> (8 * byte)) & 0xff);
				if (c == 0) {
					terminated = true;
					break;
				}
				text.push_back(c);
			}
			if (terminated) {
				++at;
				break;
			}
		}
		end = at;
		return text;
	}
};

bool Indium::spirvWithoutStencilExport(std::vector<uint32_t>& words, std::unordered_set<std::string>& writers) {
	if (words.size() < headerWords) {
		return false;
	}

	// the variables decorated as the stencil reference, and whether the module declares the capability
	bool declared = false;
	std::unordered_set<uint32_t> variables;
	for (size_t at = headerWords; at < words.size();) {
		uint32_t count = words[at] >> 16;
		uint32_t opcode = words[at] & 0xffff;
		if (count == 0 || at + count > words.size()) {
			return false;
		}
		if (opcode == opCapability && count == 2 && words[at + 1] == capabilityStencilExport) {
			declared = true;
		} else if (opcode == opDecorate && count == 4 && words[at + 2] == decorationBuiltIn && words[at + 3] == builtInFragStencilRef) {
			variables.insert(words[at + 1]);
		} else if (opcode == opMemberDecorate && count == 5 && words[at + 3] == decorationBuiltIn && words[at + 4] == builtInFragStencilRef) {
			// the member of an output block: the block cannot be made private by itself
			return false;
		}
		at += count;
	}
	if (!declared) {
		return false;
	}

	// what each variable points to, and who uses the variables
	std::unordered_map<uint32_t, uint32_t> outputPointees;
	std::unordered_map<uint32_t, uint32_t> variableTypes;
	std::unordered_set<std::string> found;
	bool inFunctions = false;
	for (size_t at = headerWords; at < words.size();) {
		uint32_t count = words[at] >> 16;
		uint32_t opcode = words[at] & 0xffff;
		inFunctions = inFunctions || opcode == opFunction;
		if (opcode == opTypePointer && count == 4) {
			if (words[at + 2] == storageClassOutput) {
				outputPointees[words[at + 1]] = words[at + 3];
			}
		} else if (opcode == opVariable && count >= 4 && variables.count(words[at + 2]) != 0) {
			if (words[at + 3] != storageClassOutput) {
				return false;
			}
			variableTypes[words[at + 2]] = words[at + 1];
		} else if (inFunctions && opcode != opLoad && opcode != opStore && opcode != opCopyMemory && opcode != opCopyMemorySized) {
			// anything else that takes the variable (an access chain, a copy of the pointer) would have a result of the
			// output pointer type, which the variable no longer is; a literal that happens to equal its id is taken
			// for it too, which only leaves the module as it is
			for (size_t i = at + 1; i < at + count; ++i) {
				if (variables.count(words[i]) != 0) {
					return false;
				}
			}
		} else if (opcode == opEntryPoint && count >= 4) {
			size_t interface = 0;
			auto name = literalString(words.data(), at + 3, at + count, interface);
			for (size_t i = interface; i < at + count; ++i) {
				if (variables.count(words[i]) != 0) {
					found.insert(name);
				}
			}
		}
		at += count;
	}
	for (auto variable: variables) {
		auto type = variableTypes.find(variable);
		if (type == variableTypes.end() || outputPointees.count(type->second) == 0) {
			return false;
		}
	}

	// From SPIR-V 1.4 an entry point lists every global variable it uses; before, only its inputs and outputs.
	const bool listsPrivate = words[1] >= version1_4;
	uint32_t bound = words[3];
	// the private pointer types declared so far, by what they point to: only those may be used for a variable
	std::unordered_map<uint32_t, uint32_t> privatePointers;
	std::vector<uint32_t> result(words.begin(), words.begin() + headerWords);
	result.reserve(words.size() + 4 * variables.size());
	for (size_t at = headerWords; at < words.size();) {
		uint32_t count = words[at] >> 16;
		uint32_t opcode = words[at] & 0xffff;
		auto first = words.begin() + at;
		at += count;

		if (opcode == opCapability && count == 2 && first[1] == capabilityStencilExport) {
			continue;
		}
		if (opcode == opExtension) {
			size_t end = 0;
			if (literalString(words.data(), at - count + 1, at, end) == "SPV_EXT_shader_stencil_export") {
				continue;
			}
		}
		if (opcode == opExecutionMode && count == 3 && first[2] == executionModeStencilRefReplacing) {
			continue;
		}
		if (opcode == opDecorate && count >= 3 && variables.count(first[1]) != 0) {
			// the built-in, and any other decoration of an output (such as Invariant), which a private variable cannot have
			continue;
		}
		if (opcode == opTypePointer && count == 4 && first[2] == storageClassPrivate) {
			privatePointers.emplace(first[3], first[1]);
		}
		if (opcode == opEntryPoint && !listsPrivate) {
			size_t interface = 0;
			literalString(words.data(), at - count + 3, at, interface);
			size_t start = result.size();
			result.insert(result.end(), first, words.begin() + interface);
			for (size_t i = interface; i < at; ++i) {
				if (variables.count(words[i]) == 0) {
					result.push_back(words[i]);
				}
			}
			result[start] = static_cast<uint32_t>(result.size() - start) << 16 | opcode;
			continue;
		}
		if (opcode == opVariable && count >= 4 && variables.count(first[2]) != 0) {
			uint32_t pointee = outputPointees[first[1]];
			auto pointer = privatePointers.find(pointee);
			if (pointer == privatePointers.end()) {
				// declared here, after the output pointer type and so after the type it points to
				uint32_t id = bound++;
				result.insert(result.end(), { 4u << 16 | opTypePointer, id, storageClassPrivate, pointee });
				pointer = privatePointers.emplace(pointee, id).first;
			}
			size_t start = result.size();
			result.insert(result.end(), first, first + count);
			result[start + 1] = pointer->second;
			result[start + 3] = storageClassPrivate;
			continue;
		}
		result.insert(result.end(), first, first + count);
	}
	result[3] = bound;

	words.swap(result);
	writers.insert(found.begin(), found.end());
	return true;
};
