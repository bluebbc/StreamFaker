
#include <stdio.h>

extern "C"{
#include "libavformat/avformat.h"
#include "libavformat/avio.h"

#include "libavcodec/avcodec.h"

#include "libavutil/avassert.h"
#include "libavutil/avstring.h"
#include "libavutil/frame.h"
#include "libavutil/opt.h"

}
#ifdef WIN32
#include "getopt.h"
#else
#include <unistd.h>
#endif
#include <string>

using namespace std;

/** The output bit rate in kbit/s */
#define OUTPUT_BIT_RATE 96000
/** The number of output channels */
#define OUTPUT_CHANNELS 2

/**
* Convert an error code into a text message.
* @param error Error code to be converted
* @return Corresponding error text (not thread-safe)
*/
static const char *get_error_text(const int error)
{
	static char error_buffer[255];
	av_strerror(error, error_buffer, sizeof(error_buffer));
	return error_buffer;
}

/**
* Open an output file and the required encoder.
* Also set some basic encoder parameters.
* Some of these parameters are based on the input file's parameters.
*/
static int open_output_file(const char *filename,
	AVFormatContext **output_format_context,
	AVCodecContext **output_codec_context,
	int sample_rate, int channel)
{
	AVCodecContext *avctx = NULL;
	AVIOContext *output_io_context = NULL;
	AVStream *stream = NULL;
	AVCodec *output_codec = NULL;
	int error;

	/** Open the output file to write to it. */
	if ((error = avio_open(&output_io_context, filename,
		AVIO_FLAG_WRITE)) < 0) {
		fprintf(stderr, "Could not open output file '%s' (error '%s')\n",
			filename, get_error_text(error));
		return error;
	}

	/** Create a new format context for the output container format. */
	if (!(*output_format_context = avformat_alloc_context())) {
		fprintf(stderr, "Could not allocate output format context\n");
		return AVERROR(ENOMEM);
	}

	/** Associate the output file (pointer) with the container format context. */
	(*output_format_context)->pb = output_io_context;

	/** Guess the desired container format based on the file extension. */
	if (!((*output_format_context)->oformat = av_guess_format(NULL, filename,
		NULL))) {
		fprintf(stderr, "Could not find output file format\n");
		goto cleanup;
	}

	av_strlcpy((*output_format_context)->filename, filename,
		sizeof((*output_format_context)->filename));

	/** Find the encoder to be used by its name. */
	if (!(output_codec = avcodec_find_encoder(AV_CODEC_ID_AAC))) {
		fprintf(stderr, "Could not find an AAC encoder.\n");
		goto cleanup;
	}

	/** Create a new audio stream in the output file container. */
	if (!(stream = avformat_new_stream(*output_format_context, NULL))) {
		fprintf(stderr, "Could not create new stream\n");
		error = AVERROR(ENOMEM);
		goto cleanup;
	}

	avctx = avcodec_alloc_context3(output_codec);
	if (!avctx) {
		fprintf(stderr, "Could not allocate an encoding context\n");
		error = AVERROR(ENOMEM);
		goto cleanup;
	}

	/**
	* Set the basic encoder parameters.
	* The input file's sample rate is used to avoid a sample rate conversion.
	*/
	avctx->channels = channel;
	avctx->channel_layout = av_get_default_channel_layout(channel);
	avctx->sample_rate = sample_rate;// input_codec_context->sample_rate;
	avctx->sample_fmt = output_codec->sample_fmts[0];
	avctx->bit_rate = OUTPUT_BIT_RATE;

	/** Allow the use of the experimental AAC encoder */
	avctx->strict_std_compliance = FF_COMPLIANCE_EXPERIMENTAL;

	/** Set the sample rate for the container. */
	stream->time_base.den = sample_rate;//input_codec_context->sample_rate;
	stream->time_base.num = 1;

	/**
	* Some container formats (like MP4) require global headers to be present
	* Mark the encoder so that it behaves accordingly.
	*/
	if ((*output_format_context)->oformat->flags & AVFMT_GLOBALHEADER)
		avctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

	/** Open the encoder for the audio stream to use it later. */
	if ((error = avcodec_open2(avctx, output_codec, NULL)) < 0) {
		fprintf(stderr, "Could not open output codec (error '%s')\n",
			get_error_text(error));
		goto cleanup;
	}

	error = avcodec_parameters_from_context(stream->codecpar, avctx);
	if (error < 0) {
		fprintf(stderr, "Could not initialize stream parameters\n");
		goto cleanup;
	}

	/** Save the encoder context for easier access later. */
	*output_codec_context = avctx;

	return 0;

cleanup:
	avcodec_free_context(&avctx);
	avio_closep(&(*output_format_context)->pb);
	avformat_free_context(*output_format_context);
	*output_format_context = NULL;
	return error < 0 ? error : AVERROR_EXIT;
}


/** Write the header of the output file container. */
static int write_output_file_header(AVFormatContext *output_format_context)
{
	int error;
	if ((error = avformat_write_header(output_format_context, NULL)) < 0) {
		fprintf(stderr, "Could not write output file header (error '%s')\n",
			get_error_text(error));
		return error;
	}
	return 0;
}

/**
* Initialize one input frame for writing to the output file.
* The frame will be exactly frame_size samples large.
*/
static int init_output_frame(AVFrame **frame,
	AVCodecContext *output_codec_context,
	int frame_size)
{
	int error;

	/** Create a new frame to store the audio samples. */
	if (!(*frame = av_frame_alloc())) {
		fprintf(stderr, "Could not allocate output frame\n");
		return AVERROR_EXIT;
	}

	/**
	* Set the frame's parameters, especially its size and format.
	* av_frame_get_buffer needs this to allocate memory for the
	* audio samples of the frame.
	* Default channel layouts based on the number of channels
	* are assumed for simplicity.
	*/
	(*frame)->nb_samples = frame_size;
	(*frame)->channel_layout = output_codec_context->channel_layout;
	(*frame)->format = output_codec_context->sample_fmt;
	(*frame)->sample_rate = output_codec_context->sample_rate;

	/**
	* Allocate the samples of the created frame. This call will make
	* sure that the audio frame can hold as many samples as specified.
	*/
	if ((error = av_frame_get_buffer(*frame, 0)) < 0) {
		fprintf(stderr, "Could allocate output frame samples (error '%s')\n",
			get_error_text(error));
		av_frame_free(frame);
		return error;
	}

	return 0;
}

/** Global timestamp for the audio frames */
static int64_t pts = 0;

/** Write the trailer of the output file container. */
static int write_output_file_trailer(AVFormatContext *output_format_context)
{
	int error;
	if ((error = av_write_trailer(output_format_context)) < 0) {
		fprintf(stderr, "Could not write output file trailer (error '%s')\n",
			get_error_text(error));
		return error;
	}
	return 0;
}

/** Convert an audio file to an AAC file in an MP4 container. */
int main(int argc, char **argv)
{
	int _channels = -1;
	int _timeLen = 0;
	int _freq = 0;
	string _outFilename;

	int ch;
	opterr = 0;
	while ((ch = getopt(argc, argv, "o:f:c:t:h")) != -1)
	{
		switch (ch)
		{
		case 'o':
			_outFilename = optarg;
			break;
		case 'f':
			_freq = atoi(optarg);
			break;
		case 'c':
			_channels = atoi(optarg);
			break;
		case 't':
			_timeLen = atoi(optarg);
			break;
		case 'h':
			printf("%s -f freq -c channels -t timeLen -o outputFileName\n", argv[0]);
			return 0;
			break;
		default:
			printf("other option :%c\n", ch);
		}
	}

	if (_channels == -1 || _timeLen == 0 || _freq == 0 || _outFilename.size() == 0)
	{
		printf("%s -f freq -c channels -t timeLen -o outputFileName\n", argv[0]);
		return 0;
	}

	AVFormatContext *output_format_context = NULL;
	AVCodecContext *output_codec_context = NULL;
	int ret = AVERROR_EXIT;
	int size = 0;
	int count = 0;

	/** Register all codecs and formats so that they can be used. */
	av_register_all();

	/** Open the output file for writing. */
	if (open_output_file(_outFilename.c_str(), &output_format_context, &output_codec_context, _freq, _channels))
	{
		return -1;
	}

	/** Write the header of the output file container. */
	if (write_output_file_header(output_format_context))
	{
		return -1;
	}

	/**
	* Loop as long as we have input samples to read or output samples
	* to write; abort as soon as we have neither.
	*/
	uint8_t* frame_buf = NULL;
	AVFrame* pFrame = NULL;
	AVPacket pkt;
#if 0
	init_output_frame(&pFrame, output_codec_context, output_codec_context->frame_size);
#else
	pFrame = av_frame_alloc();
	pFrame->nb_samples = output_codec_context->frame_size;
	pFrame->format = output_codec_context->sample_fmt;
#endif
	size = av_samples_get_buffer_size(NULL, output_codec_context->channels, output_codec_context->frame_size, output_codec_context->sample_fmt, 1);
	frame_buf = (uint8_t *)av_malloc(size);
	avcodec_fill_audio_frame(pFrame, output_codec_context->channels, output_codec_context->sample_fmt, (const uint8_t*)frame_buf, size, 1);

	av_new_packet(&pkt, size);
	int got_frame = 0;
	count = 0;
	count = _freq  * _timeLen / output_codec_context->frame_size;

	for (int i = 0; i < count; i++){
		pFrame->data[0] = frame_buf;  //PCM Data
		pFrame->linesize[0] = size / 2;
		pFrame->pts = i * 100;

		got_frame = 0;
		//Encode
		ret = avcodec_encode_audio2(output_codec_context, &pkt, pFrame, &got_frame);
		if (ret < 0){
			printf("Failed to encode!\n");
			return -1;
		}
		if (got_frame == 1){

			//printf("size:%d pts:%I64d\n", pkt.size, pkt.pts);
			pkt.stream_index = 0;
			ret = av_interleaved_write_frame(output_format_context, &pkt);

			av_free_packet(&pkt);
		}
	}

	/** Write the trailer of the output file container. */
	if (write_output_file_trailer(output_format_context))
		goto cleanup;
	ret = 0;

cleanup:
	if (output_codec_context)
		avcodec_free_context(&output_codec_context);
	if (output_format_context) {
		avio_closep(&output_format_context->pb);
		avformat_free_context(output_format_context);
	}
	return ret;
}
