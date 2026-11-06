#pragma once

#include <indium/base.hpp>
#include <indium/types.hpp>

#include <memory>
#include <string>
#include <vector>

namespace Indium {
	class Device;
	class Library;

	/**
	 * A direct member of a structure a function takes in a buffer, as the shader declares it.
	 */
	struct ArgumentMemberInfo {
		size_t offset = 0;
		std::string typeName;
		std::string name;
	};

	enum class ArgumentKind {
		Buffer,
		Texture,
		Sampler,
	};

	/**
	 * A buffer, texture or sampler argument of a function, as the shader declares it. `index` is the
	 * Metal argument table index; `size` and `alignment` describe a buffer's pointee type and are zero
	 * for textures and samplers.
	 */
	struct ArgumentInfo {
		ArgumentKind kind = ArgumentKind::Buffer;
		size_t index = 0;
		std::string name;
		std::string typeName;
		size_t size = 0;
		size_t alignment = 0;
		std::vector<ArgumentMemberInfo> members;
	};

	class Function {
	public:
		virtual ~Function() = 0;

		virtual std::shared_ptr<Device> device() = 0;

		virtual FunctionType functionType() const = 0;

		// the buffer, texture and sampler arguments the function declares, for pipeline reflection
		virtual const std::vector<ArgumentInfo>& argumentInfo() const = 0;

		INDIUM_PROPERTY_READONLY(std::string, n, N,ame);
	};

	class Library {
	public:
		virtual ~Library() = 0;

		virtual std::shared_ptr<Function> newFunction(const std::string& name) = 0;

		virtual std::shared_ptr<Device> device() = 0;
	};
};
