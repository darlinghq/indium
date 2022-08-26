#pragma once

namespace Indium {
	class CommandEncoder {
	public:
		virtual ~CommandEncoder() = 0;

		virtual void endEncoding() = 0;
	};
};
