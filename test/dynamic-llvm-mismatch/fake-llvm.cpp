// Stands in for a libLLVM of a different major than the headers the test was built with.
extern "C" void LLVMGetVersion(unsigned* major, unsigned* minor, unsigned* patch) {
	*major = 1;
	*minor = 0;
	*patch = 0;
}
