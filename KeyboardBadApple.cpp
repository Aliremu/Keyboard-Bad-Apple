#define CORSAIR_LIGHTING_SDK_DISABLE_DEPRECATION_WARNINGS
#define PL_MPEG_IMPLEMENTATION
#include "KeyboardBadApple.h"


using namespace std;

struct KeyData {
	int id;
	int x;
	int y;
	CorsairLedColor* pColor;
};

const char* ToString(CorsairError error) {
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

std::vector<CorsairLedColor> GetAvailableKeys() {
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

void video_callback(plm_t* plm, plm_frame_t* frame, void* user);

template <typename T>
void write(stringstream& stream, const T& t) {
	stream.write((const char*)&t, sizeof(T));
}

std::string BuildWav(float* data, uint32_t size) {
#define SUBCHUNK1SIZE   (16)
#define AUDIO_FORMAT    (1) /*For PCM*/
#define NUM_CHANNELS    (2)
#define SAMPLE_RATE     (44100)

#define BITS_PER_SAMPLE (16)

#define BYTE_RATE       (SAMPLE_RATE * NUM_CHANNELS * BITS_PER_SAMPLE / 8)
#define BLOCK_ALIGN     (NUM_CHANNELS * BITS_PER_SAMPLE / 8)

	stringstream s;

	int subchunk2_size = size * NUM_CHANNELS * BITS_PER_SAMPLE / 8;
	int chunk_size = 4 + (8 + SUBCHUNK1SIZE) + (8 + subchunk2_size);

	s.write("RIFF", 4);                    // RIFF chunk
	write<int>(s, chunk_size); // RIFF chunk size in bytes
	s.write("WAVE", 4);                    // WAVE chunk
	s.write("fmt ", 4);                    // fmt chunk
	write<uint32_t>(s, SUBCHUNK1SIZE);                     // size of fmt chunk
	write<uint16_t>(s, AUDIO_FORMAT);                       // Format = PCM
	write<uint16_t>(s, NUM_CHANNELS);                       // # of Channels
	write<uint32_t>(s, SAMPLE_RATE);                // Sample Rate
	write<uint32_t>(s, BYTE_RATE);    // Byte rate
	write<uint16_t>(s, BLOCK_ALIGN);             // Frame size
	write<uint16_t>(s, BITS_PER_SAMPLE);                      // Bits per sample

	s.write("data", 4);                   // data chunk
	write<uint32_t>(s, subchunk2_size);   // data chunk size in bytes
	for(size_t i = 0; i < size; i++) {
		write<int16_t>(s, (int16_t)(data[i * 2] * 32767));
		write<int16_t>(s, (int16_t)(data[i * 2 + 1] * 32767));
	}

	return s.str();
}

void audio_callback(plm_t* plm, plm_samples_t* samples, void* user) {
	string annoying = BuildWav(samples->interleaved, samples->count);
	const char* data = annoying.c_str();
}

struct App {
	//[0.0, 1.0]
	float Volume;

	int KeyboardWidth;
	int KeyboardHeight;

	int FixedKeyboardWidth;

	int KeyboardLeft;
	int KeyboardTop;

	int BoundLeft;
	int BoundRight;

	int VideoWidth;
	int VideoHeight;

	std::atomic_bool IsRunning;

	std::vector<CorsairLedColor> CorsairKeys;
	std::vector<KeyData> KeyList;

	App() noexcept {
		KeyboardWidth = 0;
		KeyboardHeight = 0;

		FixedKeyboardWidth = 0;

		KeyboardLeft = std::numeric_limits<int>::max();
		KeyboardTop = std::numeric_limits<int>::max();

		BoundLeft = 0;
		BoundRight = 0;

		VideoWidth = 0;
		VideoHeight = 0;

		IsRunning = {false};
	}

	void Start(const std::string& video) {
		CorsairPerformProtocolHandshake();
		if(const auto error = CorsairGetLastError()) {
			printf("Handshake failed: %s\nPress any key to quit.\n", ToString(error));
			getchar();
			return;
		}

		IsRunning = {true};

		CorsairKeys = GetAvailableKeys();
		if(CorsairKeys.empty()) {
			return;
		}

		CorsairLedPositions* ledPositions = CorsairGetLedPositions();
		std::vector<CorsairLedPosition> positions(ledPositions->pLedPosition, ledPositions->pLedPosition + ledPositions->numberOfLed);

		for(const auto& pos : positions) {
			KeyData data;
			data.id = pos.ledId;
			data.x = (int) pos.left;
			data.y = (int) pos.top;

			for(auto& colors : CorsairKeys) {
				if(colors.ledId == pos.ledId) {
					data.pColor = &colors;
					break;
				}
			}

			if(pos.left < KeyboardLeft) KeyboardLeft = (int) pos.left;
			if(pos.top < KeyboardTop) KeyboardTop = (int) pos.top;

			if(pos.left > KeyboardWidth) KeyboardWidth = (int) pos.left;
			if(pos.top > KeyboardHeight) KeyboardHeight = (int) pos.top;

			KeyList.push_back(data);
		}

		KeyboardWidth -= KeyboardLeft;
		KeyboardHeight -= KeyboardLeft;

		plm_t* plm = plm_create_with_filename(video.c_str());
		if(!plm) {
			printf("Couldn't open file %s\n", "bad_apple.mpg");
			return;
		}

		float fps = (float) plm_get_framerate(plm);
		printf("FPS: %f\n", fps);

		float keyboardAspectRatio = KeyboardWidth / (float)KeyboardHeight;

		plm_set_audio_enabled(plm, true);
		//plm_set_audio_stream(plm, 0);

		plm_set_video_decode_callback(plm, video_callback, this);
		//plm_set_audio_decode_callback(plm, audio_callback, nullptr);

		std::vector<float> data;

		plm_samples_t* sample;
		for(; sample = plm_decode_audio(plm);) {
			for(size_t i = 0; i < sample->count * 2; i++) {
				data.push_back(sample->interleaved[i] * Volume);
			}
		}

		plm_rewind(plm);

		string annoying = BuildWav(data.data(), (uint32_t)data.size() / 2);

		VideoWidth = plm_get_width(plm);
		VideoHeight = plm_get_height(plm);
		float videoAspectRatio = VideoWidth / (float)VideoHeight;

		//Hard-coded aspect ratio. KeyboardWidth * 2.66f;
		FixedKeyboardWidth = KeyboardWidth;

		BoundLeft = (int)((KeyboardWidth - FixedKeyboardWidth) / 2.0f);
		BoundRight = (int) ((KeyboardWidth - FixedKeyboardWidth) / 2.0f + FixedKeyboardWidth);

		std::thread lightingThread([&] {
			PlaySoundA(annoying.c_str(), NULL, SND_ASYNC | SND_MEMORY);

			std::clock_t lastTime = std::clock();

			while(IsRunning) {
				std::clock_t current = std::clock();
				double elapsedTime = current - lastTime;

				if(elapsedTime > 1000.0f / fps) {
					elapsedTime = 1000.0f / fps;
				}

				lastTime = current;

				plm_decode(plm, elapsedTime / 1000.0f);
			}
		});

		lightingThread.join();
	}
};

void video_callback(plm_t* plm, plm_frame_t* frame, void* user) {
	App& app = *reinterpret_cast<App*>(user);
	uint8_t* rgb_buffer = (uint8_t*)malloc(frame->width * frame->height * 3);;
	plm_frame_to_rgb(frame, rgb_buffer, frame->width * 3);

	for(auto& data : app.KeyList) {
		if(data.x < app.BoundLeft || data.x > app.BoundRight) continue;

		int x = (int) (((data.x - app.BoundLeft - app.KeyboardLeft) / (float)app.FixedKeyboardWidth) * app.VideoWidth);

		int y = std::min((int)(((data.y - app.KeyboardTop) / (float)app.KeyboardHeight) * app.VideoHeight), (int)(frame->height - 1));
		int index = 3 * x + y * frame->width * 3;
		int r = rgb_buffer[index];
		int g = rgb_buffer[index + 1];
		int b = rgb_buffer[index + 2];

		data.pColor->r = r;
		data.pColor->g = g;
		data.pColor->b = b;
	}
	CorsairSetLedsColorsAsync(static_cast<int>(app.CorsairKeys.size()), app.CorsairKeys.data(), nullptr, nullptr);
}

int main(int argc, char* argv[]) {
	App app;
	app.Volume = 0.5f;

	std::string video = "bad_apple.mpg";
	if(argc > 1) video = argv[1];

	app.Start(video);

	while(app.IsRunning) {
		char c = getchar();
		if(c == 'q') app.IsRunning = {false};
	}

	return 0;
}
