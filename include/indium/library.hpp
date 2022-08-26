#pragma once

#include <indium/base.hpp>

#include <memory>
#include <string>
#include <vector>

namespace Indium {
	class Device;
	class Library;

	class Function {
	public:
		virtual ~Function() = 0;

		virtual std::shared_ptr<Device> device() = 0;

		INDIUM_PROPERTY_READONLY(std::string, n, N,ame);
	};

	class Library {
	public:
		virtual ~Library() = 0;

		virtual std::shared_ptr<Function> newFunction(const std::string& name) = 0;

		virtual std::shared_ptr<Device> device() = 0;
	};
};
