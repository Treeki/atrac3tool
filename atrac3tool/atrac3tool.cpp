#include <tchar.h>
#include <stdio.h>
#include <Windows.h>
#include <mmreg.h>
#include <MSAcm.h>

BOOL CALLBACK acmDriverEnumCallback(HACMDRIVERID hadid, DWORD_PTR dwInstance, DWORD fdwSupport)
{
	ACMDRIVERDETAILS details;
	details.cbStruct = sizeof(details);
	acmDriverDetails(hadid, &details, 0);

	if (wcscmp(details.szShortName, L"ATRAC3") == 0) {
		_tprintf(L"ATRAC3 located: %s / %s / %s\n", details.szShortName, details.szLongName, details.szFeatures);
		*(HACMDRIVERID *)dwInstance = hadid;
		return FALSE;
	}

	return TRUE;
}

int main(int argc, char **argv)
{
	if (argc != 3) {
		_tprintf(L"Usage: %hs [inFile.wav] [outFile.wav]\n", argv[0]);
		return 0;
	}

	HACMDRIVERID atrac3Id = nullptr;
	acmDriverEnum(acmDriverEnumCallback, (DWORD_PTR)&atrac3Id, 0);

	if (atrac3Id == nullptr) {
		_tprintf(L"ATRAC3 not found!\n");
		return 1;
	}

	HACMDRIVER atrac3;
	MMRESULT openRes = acmDriverOpen(&atrac3, atrac3Id, 0);
	if (openRes != 0) {
		_tprintf(L"acmDriverOpen failed!\n");
		return 1;
	}

	//const char *inpath = "Z:\\Data\\reversing\\atrac3\\broken.wav";
	const char *inpath = argv[1];

	FILE *input = fopen(inpath, "rb");
	if (!input) {
		_tprintf(L"cannot open input file\n");
		return 0;
	}

	unsigned char *bFmt = nullptr, *bData = nullptr;
	unsigned int sizeFmt = 0, sizeData = 0;

	char c[12];
	fread(c, 1, 12, input);
	if (memcmp(c, "RIFF", 4) || memcmp(&c[8], "WAVE", 4)) {
		_tprintf(L"Not a WAV!");
		return 1;
	}

	while (!feof(input)) {
		char magic[4];
		DWORD32 blockSize;
		fread(magic, 1, 4, input);
		if (feof(input)) break;
		fread(&blockSize, 4, 1, input);
		if (feof(input)) break;

		if (memcmp(magic, "fmt ", 4) == 0 && !bFmt) {
			bFmt = new unsigned char[blockSize];
			sizeFmt = blockSize;
			fread(bFmt, 1, sizeFmt, input);
			_tprintf(L"Read fmt section (%u bytes)\n", blockSize);
		}
		else if (memcmp(magic, "data", 4) == 0 && !bData) {
			bData = new unsigned char[blockSize];
			sizeData = blockSize;
			fread(bData, 1, sizeData, input);
			_tprintf(L"Read data section (%u bytes)\n", blockSize);
		}
		else {
			fseek(input, blockSize, SEEK_CUR);
		}
	}

	if (sizeFmt != 0x10) {
		_tprintf(L"Unknown fmt size!\n");
		return 1;
	}

	HACMSTREAM stream;
	PCMWAVEFORMAT *pStoredFmt = (PCMWAVEFORMAT *)bFmt;
	WAVEFORMATEX srcFormat;
	char dstFormatBuf[0x20];
	WAVEFORMATEX *pDstFormat = (WAVEFORMATEX *)dstFormatBuf;
	srcFormat.wFormatTag = pStoredFmt->wf.wFormatTag;
	srcFormat.nChannels = pStoredFmt->wf.nChannels;
	srcFormat.nSamplesPerSec = pStoredFmt->wf.nSamplesPerSec;
	srcFormat.nAvgBytesPerSec = pStoredFmt->wf.nAvgBytesPerSec;
	srcFormat.nBlockAlign = pStoredFmt->wf.nBlockAlign;
	srcFormat.wBitsPerSample = pStoredFmt->wBitsPerSample;
	srcFormat.cbSize = 0;

	pDstFormat->wFormatTag = WAVE_FORMAT_SONY_SCX;
	pDstFormat->nChannels = 2;
	pDstFormat->nSamplesPerSec = 44100;
	pDstFormat->nAvgBytesPerSec = 16537;
	pDstFormat->nBlockAlign = 0x180;
	pDstFormat->wBitsPerSample = 0;
	pDstFormat->cbSize = 0xE;
	memcpy(&dstFormatBuf[0x12], "\x01\x00\x00\x10\x00\x00\x00\x00\x00\x00\x01\x00\x00\x00", 0xE);

	MMRESULT streamRes = acmStreamOpen(&stream, atrac3, &srcFormat, pDstFormat, nullptr, 0, 0, 0);
	if (streamRes != 0) {
		_tprintf(L"acmStreamOpen failed %08x\n", streamRes);
		return 1;
	}

	DWORD outputByteSize;
	MMRESULT sizeRes = acmStreamSize(stream, sizeData, &outputByteSize, ACM_STREAMSIZEF_SOURCE);
	if (sizeRes != 0) {
		_tprintf(L"acmStreamSize failed %08x\n", sizeRes);
		return 1;
	}
	_tprintf(L"streamSize recommends %08x\n", outputByteSize);
	outputByteSize += 0x400;

	unsigned char *result = new unsigned char[outputByteSize];

	ACMSTREAMHEADER ash;
	ash.cbStruct = sizeof(ash);
	ash.fdwStatus = 0;
	ash.pbSrc = bData;
	ash.cbSrcLength = sizeData;
	ash.cbSrcLengthUsed = 0;
	ash.pbDst = result;
	ash.cbDstLength = outputByteSize;
	ash.cbDstLengthUsed = 0;

	MMRESULT prepRes = acmStreamPrepareHeader(stream, &ash, 0);
	if (prepRes != 0) {
		_tprintf(L"acmStreamPrepareHeader failed %08x\n", prepRes);
		return 1;
	}

	MMRESULT convRes = acmStreamConvert(stream, &ash, ACM_STREAMCONVERTF_BLOCKALIGN);
	if (convRes != 0) {
		_tprintf(L"acmStreamConvert failed %08x\n", convRes);
		return 1;
	}

	acmStreamUnprepareHeader(stream, &ash, 0);
	acmStreamClose(stream, 0);

	_tprintf(L"converted? %08x\n", ash.cbDstLengthUsed);

	// write the thing
	//const char *outPath = "Z:\\Data\\reversing\\atrac3\\brokenConv.wav";
	const char *outPath = argv[2];
	FILE *output = fopen(outPath, "wb");
	if (!output) {
		_tprintf(L"cannot open output file\n");
		return 1;
	}

	DWORD32 i = 0xC + 8 + 0x20 + 8 + ash.cbDstLengthUsed;
	fwrite("RIFF", 1, 4, output);
	fwrite(&i, 4, 1, output);
	fwrite("WAVE", 1, 4, output);

	fwrite("fmt ", 1, 4, output);
	i = 0x20;
	fwrite(&i, 4, 1, output);
	fwrite(dstFormatBuf, 1, 0x20, output);

	fwrite("data", 1, 4, output);
	i = ash.cbDstLengthUsed;
	fwrite(&i, 4, 1, output);
	fwrite(result, 1, ash.cbDstLengthUsed, output);

	_tprintf(L"done!!\n");
}

