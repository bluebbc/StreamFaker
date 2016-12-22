#pragma once
#include<stdio.h>
extern "C"
{
#include "libavformat/avformat.h"
};

#include <string>
#include <vector>

struct Config{
	Config(){}
	~Config(){}

	void MakeFileName()
	{
		std::string::size_type found = _inputFile.find_last_of("/\\");
		_inputFileName = _inputFile.substr(found + 1);
	}

	std::string _inputFile;
	std::string _inputFileName;
	std::string _outputPath;
	std::string _outputFile;
	std::string _inputAudioFile;
};



typedef struct nal_s{
	unsigned char type;
	bool bHasStartCode;
	int posHead;
	int size;
	std::vector<nal_s> vecSons;
}nalu_s, pktu_s;

#define USE_H264BSF 0
#define USE_AACBSF 0
#define IO_BUFFER_SIZE 32768  

enum error_no_e{
	ErrorNo_OK = 0,
	ErrorNo_FileOpenFail,
	ErrorNo_NoAudio,
	ErrorNo_NoVideo,
	ErrorNo_NoVideoOrAudio,
	ErrorNo_Unknow
};

typedef struct InputCfg_S{
	std::string filename;
	//FILE *pFile;
	AVFormatContext *ifmt_ctx;
	AVInputFormat *ifmt;
	int index;
}InputCfg_S;

typedef struct OutputCfg_S{
	std::string filename;
	AVFormatContext *ofmt_ctx;
	AVOutputFormat *ofmt;
	int indexVideo;
	int indexAudio;
}OutputCfg_S;

typedef struct media_config
{
	int audio_bit_rate;
	int audio_sample_rate;
	int audio_channels;
	int frame_rate;
	int bit_rate;
	int width;
	int height;
}media_config_s;

class mp4Writer
{
public:
	mp4Writer();
	~mp4Writer();

	int writeMp4(Config &cfg);

private:
	int init();
	int open_input_file(InputCfg_S *p, FILE * &pFile, char *url,
		int(*read_packet)(void *opaque, uint8_t *buf, int buf_size));
	int open_output_file(OutputCfg_S *p, InputCfg_S *inV);
	int bind_stream(InputCfg_S *in, OutputCfg_S *out, int type, int &index);
	int cleanup();

	AVStream *add_stream(AVFormatContext *oc, AVCodec **codec, char *metadata, int metadataLen, media_config_s *pConf, enum AVCodecID codec_id);
private:

	InputCfg_S _inVideoCfg;
	OutputCfg_S _outputCfg;

	unsigned char *_buffer;
};
