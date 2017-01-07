
#include <stdio.h>
#include <string>

#ifdef WIN32
#include "getopt.h"
#include <Windows.h>
#else
#include <unistd.h>
#include <stdlib.h>
#endif

using namespace std;

#pragma pack(2)
typedef struct
{
	short wFormatag;//�����ʽ������WAVE_FORMAT_PCM��WAVEFORMAT_ADPCM��
	short nChannls;//��������������Ϊ1��˫����Ϊ2;
	int nSamplesPerSec;//����Ƶ�ʣ�
	int nAvgBytesperSec;//ÿ�����������
	short nBlockAlign;//����룻
}WAVEFORMAT_S;

typedef struct
{
	WAVEFORMAT_S wf;       //���θ�ʽ��
	short wBitsPerSample; //WAVE�ļ��Ĳ�����С��
}PCMWAVEFORMAT_S;
#pragma pack()  

#define MAX_BUF_SIZE	1024*1024*3
static char gBuf[MAX_BUF_SIZE] = { 0 };

static void version()
{
	printf("version : v0.3\n");
}

static int platform_check()
{

	if (sizeof(int) != 4 || sizeof(short) != 2 || sizeof(PCMWAVEFORMAT_S) != 16)
		return -1;
	return 0;
}

int main(int argc, char **argv)
{
	if (platform_check() != 0)
	{
		printf("[failed]error 0\n");
		return -1;
	}
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
			version();
			printf("%s -f freq -c channels -t timeLen -o outputFileName\n", argv[0]);
			return 0;
			break;
		default:
			printf("other option :%c\n", ch);
		}
	}

	if (_channels == -1 || _timeLen == 0 || _freq == 0 || _outFilename.size() == 0)
	{
		version();
		printf("%s -f freq -c channels -t timeLen -o outputFileName\n", argv[0]);
		return -1;
	}

	if (_channels <= 0 ||
		_channels > 2 ||
		_freq <= 0 ||
		_timeLen <= 0)
	{
		printf("[failed]input param error 1\n");
		return -1;
	}

	if ((_freq*_channels * 2) > MAX_BUF_SIZE)
	{
		printf("[failed]input param error 2\n");
		return -1;
	}

	FILE *pfile = fopen(_outFilename.c_str(), "wb");

	PCMWAVEFORMAT_S format;
	format.wf.wFormatag = 1;
	format.wf.nChannls = _channels;
	format.wf.nSamplesPerSec = _freq;
	format.wf.nAvgBytesperSec = _freq*_channels * 2;
	format.wf.nBlockAlign = 4;
	format.wBitsPerSample = 16;

	int count = _timeLen;
	int audioDataSize = count * format.wf.nAvgBytesperSec;
	int dataSize = 36 + audioDataSize;

	fwrite("RIFF", 4, 1, pfile);
	fwrite(&dataSize, 4, 1, pfile);
	fwrite("WAVE", 4, 1, pfile);
	fwrite("fmt ", 4, 1, pfile);
	int tmp = sizeof(PCMWAVEFORMAT_S);
	fwrite(&tmp, 4, 1, pfile);
	fwrite(&format, tmp, 1, pfile);
	fwrite("data", 4, 1, pfile);
	fwrite(&audioDataSize, 4, 1, pfile);

	int sizePerSec = format.wf.nAvgBytesperSec;
	for (int i = 0; i < sizePerSec; i++)
		gBuf[i] = i % 3;
	for (int i = 0; i < count; i++)
	{
		fwrite(gBuf, sizePerSec, 1, pfile);
	}
	fclose(pfile);
	printf("sucessed\n");
	return 0;
}
