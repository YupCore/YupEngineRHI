#pragma once
#include <donut/core/vfs/VFS.h>
#include <nvrhi/nvrhi.h>
#include "AudioEngine.h"

struct AVFormatContext;
struct AVCodecContext;
struct SwrContext;
struct AVPacket;
struct AVCodecContext;
struct AVFrame;
struct AVIOContext;

namespace donut::engine
{
	class FramebufferFactory;
}

struct BufferContext {
	uint8_t* data;    // Pointer to the data
	size_t size;      // Total size of the data
	size_t position;  // Current read position
};

class VideoRenderer
{
	friend void decodeAudioFrame(AVPacket* packet, AVCodecContext* audioCodecCtx, SwrContext* swrCtx, int outChannels, int outSampleRate);
public:
	VideoRenderer(nvrhi::DeviceHandle device, std::shared_ptr<donut::vfs::IFileSystem> filesystem, std::string videoPath);

	void PresentFrame(const std::shared_ptr<donut::engine::FramebufferFactory>& framebufferFactory, nvrhi::CommandListHandle commandList);

	//void UninitFFMPEG();

	bool EOV;
	nvrhi::TextureHandle m_dynamicYUVSource;
private:

	void RenderThisFrameToScreen(AVFrame* videoFrame, const std::shared_ptr<donut::engine::FramebufferFactory>& framebufferFactory, nvrhi::CommandListHandle commandList);
	
	double framerate;
	AVFormatContext* formatCtx;
	AVCodecContext* audioCodecCtx;
	SwrContext* swrCtx;
	AVPacket* packet;
	AVCodecContext* videoCodecCtx;
	BufferContext* bufferContext;

	int videoStreamIndex, audioStreamIndex;

	FMOD::Sound* m_streamedSound;
	FMOD::Channel* m_streamedSoundChannel;
	bool audioStarted;

	uint8_t* avioBuffer;
	AVIOContext* avioCtx;
};