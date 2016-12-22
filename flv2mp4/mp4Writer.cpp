#include "mp4Writer.h"

#include <stdio.h>
#include <string>
#include <vector>
#include <list>

FILE *fp_open_v;

mp4Writer::mp4Writer()
{
	_inVideoCfg.ifmt = NULL;
	_outputCfg.ofmt = NULL;
	_inVideoCfg.ifmt_ctx = NULL;
	_outputCfg.ofmt_ctx = NULL;

	_inVideoCfg.index = -1;
	_outputCfg.indexVideo = -1;
	_outputCfg.indexAudio = -1;

	_buffer = new unsigned char[2*1024 * 1024];
}


mp4Writer::~mp4Writer()
{
	delete _buffer;
}

static int fill_iobuffer_v(void *opaque, uint8_t *buf, int buf_size)
{
	if (!feof(fp_open_v)){
		int true_size = fread(buf, 1, buf_size, fp_open_v);
		return true_size;
	}
	else{
		return -1;
	}
}

int mp4Writer::open_input_file(InputCfg_S *p, FILE * &pFile, char *url,
	int(*read_packet)(void *opaque, uint8_t *buf, int buf_size))
{
	int ret;
	unsigned char *iobuffer_v;
	AVIOContext *avio_v;

	pFile = fopen(p->filename.c_str(), "rb");
	if (pFile == NULL)
	{
		fprintf(stderr, "[failed] Could not open input file\n");
		ret = ErrorNo_FileOpenFail;
		goto end;
	}
	p->ifmt_ctx = avformat_alloc_context();
	iobuffer_v = (unsigned char *)av_malloc(IO_BUFFER_SIZE);
	avio_v = avio_alloc_context(iobuffer_v, IO_BUFFER_SIZE, 0, NULL, read_packet, NULL, NULL);
	p->ifmt_ctx->pb = avio_v;

	p->ifmt = av_find_input_format("flv");
	if ((ret = avformat_open_input(&p->ifmt_ctx, url, p->ifmt, NULL)) < 0) {
		fprintf(stderr, "[failed] Could not open input file\n");
		ret = ErrorNo_FileOpenFail;
		goto end;
	}

	if ((ret = avformat_find_stream_info(p->ifmt_ctx, 0)) < 0) {
		fprintf(stderr, "[failed] Failed to retrieve input stream information\n");
		ret = ErrorNo_Unknow;
		goto end;
	}

	return 0;
end:
	if (pFile){
		fclose(pFile);
		pFile = NULL;
	}
	if (p->ifmt_ctx){
		avformat_close_input(&p->ifmt_ctx);
		p->ifmt_ctx = NULL;
	}
	return ret;
}

int mp4Writer::bind_stream(InputCfg_S *in, OutputCfg_S *out, int type, int &outIndex)
{
	int ret;
	for (int i = 0; i < in->ifmt_ctx->nb_streams; i++) {
		//Create output AVStream according to input AVStream
		if (in->ifmt_ctx->streams[i]->codec->codec_type == type){
			AVStream *in_stream = in->ifmt_ctx->streams[i];
			AVStream *out_stream = avformat_new_stream(out->ofmt_ctx, in_stream->codec->codec);
			in->index = i;
			if (!out_stream) {
				fprintf(stderr, "[failed] Failed allocating output stream\n");

				ret = ErrorNo_Unknow;
				goto end;
			}
			outIndex = out_stream->index;
			//Copy the settings of AVCodecContext
			if (avcodec_copy_context(out_stream->codec, in_stream->codec) < 0) {
				printf("Failed to copy context from input to output stream codec context\n");
				ret = ErrorNo_Unknow;
				goto end;
			}
			out_stream->codec->codec_tag = 0;
			if (out->ofmt_ctx->oformat->flags & AVFMT_GLOBALHEADER)
				out_stream->codec->flags |= CODEC_FLAG_GLOBAL_HEADER;
			break;
		}
	}

	return 0;
end:
	return ret;
}

AVStream *mp4Writer::add_stream(AVFormatContext *oc, AVCodec **codec, char *metadata, int metadataLen, media_config_s *pConf, enum AVCodecID codec_id)
{
	AVCodecContext *c;
	AVStream *st;

	/* find the encoder */
	*codec = avcodec_find_encoder(codec_id);
	if (!(*codec)) {
		fprintf(stderr, "Could not find encoder for '%s'\n", avcodec_get_name(codec_id));
		return NULL;
	}

	st = avformat_new_stream(oc, *codec);
	if (!st) {
		fprintf(stderr, "Could not allocate stream\n");
		return NULL;
	}
	st->id = oc->nb_streams - 1;
	c = st->codec;

	switch ((*codec)->type) {
	case AVMEDIA_TYPE_AUDIO:
		c->sample_fmt = AV_SAMPLE_FMT_FLTP;
		c->bit_rate = 64000;//pConf->audio_bit_rate;
		c->sample_rate = pConf->audio_sample_rate;
		c->channels = pConf->audio_channels;
		AVRational timebasea;
		timebasea.num = 1;
		timebasea.den = pConf->audio_sample_rate;
		st->time_base = timebasea;
		st->start_time = 0;

		st->time_base = timebasea;
		break;

	case AVMEDIA_TYPE_VIDEO:
		c->codec_id = codec_id;
		c->codec_type = AVMEDIA_TYPE_VIDEO;

		AVRational timebase;
		timebase.num = 1000;
		timebase.den = pConf->frame_rate * 1000;
		st->time_base = timebase;
		c->time_base = timebase;

		AVRational framerate;
		framerate.num = pConf->frame_rate * 1000;
		framerate.den = 1000;
		st->r_frame_rate = framerate;
		st->start_time = 0;
		c->bit_rate = pConf->bit_rate;
		/* Resolution must be a multiple of two. */
		c->width = pConf->width;
		c->height = pConf->height;
		c->gop_size = pConf->frame_rate; /* emit one intra frame every twelve frames at most */
		c->pix_fmt = AV_PIX_FMT_YUV420P;
		c->extradata = (uint8_t *)metadata;
		c->extradata_size = metadataLen;
		break;

	default:
		break;
	}

	/* Some formats want stream headers to be separate. */
	if (oc->oformat->flags & AVFMT_GLOBALHEADER)
		c->flags |= CODEC_FLAG_GLOBAL_HEADER;

	return st;
}

int mp4Writer::open_output_file(OutputCfg_S *p, InputCfg_S *inV)
{
	int ret;
	media_config_s mc = { 0 };
	static AVCodec *aCodec = NULL;

	avformat_alloc_output_context2(&p->ofmt_ctx, NULL, NULL, p->filename.c_str());
	if (!p->ofmt_ctx) {
		fprintf(stderr, "[failed] Could not create output context\n");

		ret = ErrorNo_Unknow;
		goto end;
	}
	p->ofmt = p->ofmt_ctx->oformat;

	ret = bind_stream(inV, p, AVMEDIA_TYPE_VIDEO, p->indexVideo);
	//ret |= bind_stream(inA, p, AVMEDIA_TYPE_AUDIO, p->indexAudio);

	mc.audio_bit_rate = 1000;
	mc.audio_channels = 2;
	mc.audio_sample_rate = 44100;

	add_stream(p->ofmt_ctx, &aCodec, NULL, 0, &mc, AV_CODEC_ID_AAC);
	if (ret != 0)
	{
		goto end;
	}
	p->indexAudio = 1;
	if (inV->index == -1 || p->indexAudio == -1 || p->indexVideo == -1){
		ret = ErrorNo_NoVideoOrAudio;
		if (inV->index == -1)
			fprintf(stderr, "[failed] no video stream\n");
		
		goto end;
	}

	//Open output file
	if (!(p->ofmt->flags & AVFMT_NOFILE)) {
		if (avio_open(&p->ofmt_ctx->pb, p->filename.c_str(), AVIO_FLAG_WRITE) < 0) {
			printf("Could not open output file '%s'", p->filename.c_str());
			ret = ErrorNo_FileOpenFail;
			goto end;
		}
	}

	return 0;
end:
	if (p->ofmt_ctx)
	{
		if (p->ofmt_ctx && !(p->ofmt->flags & AVFMT_NOFILE))
			avio_close(p->ofmt_ctx->pb);
		avformat_free_context(p->ofmt_ctx);
		p->ofmt_ctx = NULL;
	}
	return ret;
}

int mp4Writer::init()
{
	int ret;
	av_register_all();

	ret = open_input_file(&_inVideoCfg, fp_open_v, "", fill_iobuffer_v);
	if (ret != 0){
		goto end;
	}
	ret = open_output_file(&_outputCfg, &_inVideoCfg);
	if (ret != 0){
		goto end;
	}

	return 0;
end:
	return ret;
}

int mp4Writer::cleanup()
{
	if (_inVideoCfg.ifmt_ctx){
		avformat_close_input(&_inVideoCfg.ifmt_ctx);
	}
	/* close output */
	if (_outputCfg.ofmt_ctx && !(_outputCfg.ofmt->flags & AVFMT_NOFILE))
		avio_close(_outputCfg.ofmt_ctx->pb);
	if (_outputCfg.ofmt_ctx){
		avformat_free_context(_outputCfg.ofmt_ctx);
	}

	return 0;
}

int mp4Writer::writeMp4(Config &cfg)
{
	int ret = 0;
	_inVideoCfg.filename = cfg._inputFile;
	_outputCfg.filename = cfg._outputFile;

	ret = init();
	if (ret != 0){
		cleanup();
		return ret;
	}

	int64_t cur_pts_v = 0;
	int64_t cur_pts_a = 0;
	int frame_index = 0;
	AVPacket pkt;

	// read a audio pkt
	AVRational time_base_a;
	time_base_a.num = 1;
	time_base_a.den = 1000;

	char audioBuf[1000]={0};
	AVPacket pktAudio = {0};
	av_init_packet(&pktAudio);
	pktAudio.data = (unsigned char *)audioBuf;
	pktAudio.size = 100;
	pktAudio.pos = 0;
	pktAudio.flags = 1;
	pktAudio.convergence_duration = 0;
	pktAudio.side_data_elems = 0;


	//Write file header
	if (avformat_write_header(_outputCfg.ofmt_ctx, NULL) < 0) {
		fprintf(stderr, "[failed] Error occurred when opening output file\n");
		ret = ErrorNo_Unknow;
		goto end;
	}

	while (1) {
		AVFormatContext *ifmt_ctx;
		int stream_index = 0;
		AVStream *in_stream, *out_stream;

		//Get an AVPacket
		{
			ifmt_ctx = _inVideoCfg.ifmt_ctx;
			stream_index = _outputCfg.indexVideo;

			if (av_read_frame(ifmt_ctx, &pkt) >= 0){
				do{
					in_stream = ifmt_ctx->streams[pkt.stream_index];
					out_stream = _outputCfg.ofmt_ctx->streams[stream_index];

					if (pkt.stream_index == _inVideoCfg.index){
						break;
					}else{
						av_free_packet(&pkt);
					}
				} while (av_read_frame(ifmt_ctx, &pkt) >= 0);
			}
			else{
				break;
			}
		}

		// for audio pkt
		pktAudio.pts = pkt.pts + 20;
		pktAudio.dts = pkt.dts + 20;

		pktAudio.pts = av_rescale_q_rnd(pktAudio.pts, time_base_a, out_stream->time_base, (AVRounding)(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
		pktAudio.dts = av_rescale_q_rnd(pktAudio.dts, time_base_a, out_stream->time_base, (AVRounding)(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
		pktAudio.duration = av_rescale_q(pktAudio.duration, time_base_a, out_stream->time_base);
		pktAudio.pos = -1;
		pktAudio.stream_index = 1;

		//Convert PTS/DTS
		pkt.pts = av_rescale_q_rnd(pkt.pts, in_stream->time_base, out_stream->time_base, (AVRounding)(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
		pkt.dts = av_rescale_q_rnd(pkt.dts, in_stream->time_base, out_stream->time_base, (AVRounding)(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
		pkt.duration = av_rescale_q(pkt.duration, in_stream->time_base, out_stream->time_base);
		pkt.pos = -1;
		pkt.stream_index = stream_index;

		//printf("Write 1 Packet. size:%5d\tpts:%lld\n", pkt.size, pkt.pts);
		//Write
		if (av_write_frame(_outputCfg.ofmt_ctx, &pkt) < 0) {
			fprintf(stderr, "[failed] Error muxing packet\n");
			//break;
		}

		if (av_write_frame(_outputCfg.ofmt_ctx, &pktAudio) < 0)
		{
			//fprintf(stderr, "[failed] Error muxing packet\n");
			//break;
		}

		//av_free_packet(&pktAudio);

		av_free_packet(&pkt);
	}
	//Write file trailer
	av_write_trailer(_outputCfg.ofmt_ctx);

	return 0;
end:
	cleanup();
	return ret;
}
