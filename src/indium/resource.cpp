#include <indium/resource.hpp>

Indium::Resource::~Resource() {};

std::shared_ptr<Indium::Heap> Indium::Resource::heap() {
	return nullptr;
};

size_t Indium::Resource::heapOffset() {
	return 0;
};

size_t Indium::Resource::allocatedSize() {
	return 0;
};

void Indium::Resource::makeAliasable() {};

bool Indium::Resource::isAliasable() {
	return false;
};

Indium::HazardTrackingMode Indium::Resource::hazardTrackingMode() const {
	return HazardTrackingMode::HazardTrackingModeTracked;
};
