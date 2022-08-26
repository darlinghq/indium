#pragma once

#include <memory>

namespace Indium {
	class Device;

	class Resource {
	public:
		virtual ~Resource() = 0;

		virtual std::shared_ptr<Device> device() = 0;

		// TODO: other properties
	};
};
