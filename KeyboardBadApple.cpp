#define CORSAIR_LIGHTING_SDK_DISABLE_DEPRECATION_WARNINGS
#include "KeyboardBadApple.h"

using namespace std;

const char* toString(CorsairError error) {
	switch(error) {
	case CE_Success:
		return "CE_Success";
	case CE_ServerNotFound:
		return "CE_ServerNotFound";
	case CE_NoControl:
		return "CE_NoControl";
	case CE_ProtocolHandshakeMissing:
		return "CE_ProtocolHandshakeMissing";
	case CE_IncompatibleProtocol:
		return "CE_IncompatibleProtocol";
	case CE_InvalidArguments:
		return "CE_InvalidArguments";
	default:
		return "unknown error";
	}
}

std::vector<CorsairLedColor> getAvailableKeys() {
	auto colorsSet = std::unordered_set<int>();
	for(int deviceIndex = 0, size = CorsairGetDeviceCount(); deviceIndex < size; deviceIndex++) {
		if(const auto ledPositions = CorsairGetLedPositionsByDeviceIndex(deviceIndex)) {
			for(auto i = 0; i < ledPositions->numberOfLed; i++) {
				const auto ledId = ledPositions->pLedPosition[i].ledId;
				colorsSet.insert(ledId);
			}
		}
	}

	std::vector<CorsairLedColor> colorsVector;
	colorsVector.reserve(colorsSet.size());
	for(const auto& ledId : colorsSet) {
		colorsVector.push_back({ static_cast<CorsairLedId>(ledId), 0, 0, 0 });
	}
	return colorsVector;
}

int main() {
	CorsairPerformProtocolHandshake();
	if(const auto error = CorsairGetLastError()) {
		printf("Handshake failed: %s\nPress any key to quit.\n", toString(error));
		getchar();
		return -1;
	}

	std::atomic_bool continueExecution{ true };

	auto colorsVector = getAvailableKeys();
	if(colorsVector.empty()) {
		return 1;
	}

	CorsairLedPositions* ledPositions = CorsairGetLedPositions();
	std::vector<CorsairLedPosition> positions(ledPositions->pLedPosition, ledPositions->pLedPosition + ledPositions->numberOfLed);

	struct KeyData {
		int id;
		int x;
		int y;
		CorsairLedColor* pColor;
	};

	std::vector<KeyData> keyData;

	int keyboardW = 0;
	int keyboardH = 0;

	for(const auto& pos : positions) {
		KeyData data;
		data.id = pos.ledId;
		data.x  = pos.left;
		data.y  = pos.top;

		for(auto& colors : colorsVector) {
			if(colors.ledId == pos.ledId) {
				data.pColor = &colors;
				break;
			}
		}

		if(pos.left > keyboardW) keyboardW = pos.left;
		if(pos.top > keyboardH) keyboardH = pos.top;

		keyData.push_back(data);
	}

	plm_t* plm = plm_create_with_filename("bad_apple.mpg");
	if(!plm) {
		printf("Couldn't open file %s\n", "bad_apple.mpg");
		return 1;
	}

	float fps = plm_get_framerate(plm);
	printf("FPS: %f\n", fps);

	float keyboardAspectRatio = keyboardW / (float) keyboardH;

	plm_set_audio_enabled(plm, false);

	int w = plm_get_width(plm);
	int h = plm_get_height(plm);
	float videoAspectRatio = w/(float)h;

	//Hard-coded aspect ratio.
	int fixedKeyboardW = keyboardH * 2.66f;

	int boundLeft  = (keyboardW - fixedKeyboardW) / 2.0f;
	int boundRight = (keyboardW - fixedKeyboardW) / 2.0f + fixedKeyboardW;

	uint8_t* rgb_buffer = (uint8_t*)malloc(w * h * 3);

	char png_name[16];
	plm_frame_t* frame = NULL;

	std::thread lightingThread([&] {
		while(continueExecution) {
			for(int i = 1; frame = plm_decode_video(plm); i++) {
				std::clock_t start = std::clock();

				plm_frame_to_rgb(frame, rgb_buffer, w * 3);

				for(auto& data : keyData) {
					if(data.x < boundLeft || data.x > boundRight) continue;

					int x = ((data.x - boundLeft) / (float)fixedKeyboardW) * w;

					int y = (data.y / (float) keyboardH) * h;
					int index = 3 * x + y * frame->width * 3;
					int r = rgb_buffer[index];
					int g = rgb_buffer[index + 1];
					int b = rgb_buffer[index + 2];

					data.pColor->r = r;
					data.pColor->g = g;
					data.pColor->b = b;
				}

				CorsairSetLedsColorsAsync(static_cast<int>(colorsVector.size()), colorsVector.data(), nullptr, nullptr);

				//Sleep to match video framerate. Maybe naive solution idk.
				std::clock_t end = std::clock();
				double duration = end - start;
				int wait = (1000.0f/fps) - duration;

				std::this_thread::sleep_for(std::chrono::milliseconds(wait));
			}
		}
	});

	lightingThread.join();
	return 0;
}