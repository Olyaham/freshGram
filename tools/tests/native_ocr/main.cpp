#include <tesseract/baseapi.h>

#include <iostream>

int main() {
	tesseract::TessBaseAPI engine;
	const auto version = tesseract::TessBaseAPI::Version();
	if (!version || !*version) {
		return 1;
	}
	std::cout << "Tesseract " << version << '\n';
	engine.End();
	return 0;
}
