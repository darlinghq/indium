#include <iridium/iridium.hpp>

#include <string>
#include <iostream>
#include <filesystem>
#include <fstream>

int main(int argc, char** argv) {
	if (argc < 2) {
		std::cerr << "Usage: " << argv[0] << " <input> [output]" << std::endl;
		return 1;
	}

	std::filesystem::path inPath = argv[1];
	std::filesystem::path outPathSPV;

	if (argc > 2) {
		outPathSPV = argv[2];
	} else {
		outPathSPV = inPath;
		outPathSPV.replace_extension("spv");
	}

	std::filesystem::path outPathJSON = outPathSPV;
	outPathJSON += ".json";

	std::ifstream inFile(inPath, std::ios::binary | std::ios::ate);
	std::ofstream outFileSPV(outPathSPV, std::ios::binary);
	std::ofstream outFileJSON(outPathJSON);

	auto inFileSize = inFile.tellg();
	inFile.seekg(0, std::ios::beg);

	// check the first 4 (magic) bytes first

	char magic[4];
	if (!inFile.read(magic, 4)) {
		std::cerr << "Failed to read input file" << std::endl;
		return 1;
	}

	if (magic[0] != 'M' || magic[1] != 'T' || magic[2] != 'L' || magic[3] != 'B') {
		std::cerr << "Invalid input file (not a Metal library)" << std::endl;
		return 1;
	}

	// okay, now read the entire file

	inFile.seekg(0, std::ios::beg);
	auto buffer = new char[inFileSize];
	if (!inFile.read(buffer, inFileSize)) {
		std::cerr << "Failed to read input file" << std::endl;
		return 1;
	}

	Iridium::init();

	size_t outputSize = 0;
	Iridium::OutputInfo outputInfo;
	auto result = Iridium::translate(buffer, inFileSize, outputSize, outputInfo);
	bool ok = outputSize == 0 || !!result;

	if (ok) {
		outFileSPV.write(static_cast<const char*>(result), outputSize);
		free(result);

		// TODO: write output info to JSON file
		outFileJSON << "{}" << std::endl;
	} else {
		std::cerr << "Failed to translate library" << std::endl;
	}

	Iridium::finit();

	delete[] buffer;

	return ok ? 0 : 1;
};
