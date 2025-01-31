#include "VideoRenderer.h"
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <future>
#include <fstream>
#include <queue>

extern "C" {
    #include <libavformat/avformat.h>
    #include <libavcodec/avcodec.h>
    #include <libswscale/swscale.h>
    #include <libswresample/swresample.h>
    #include <libavutil/imgutils.h>
    #include <libavutil/opt.h>
}

std::mutex audioMutex;
std::condition_variable audioCv;
std::vector<uint8_t> audioBufferQueue;
std::mutex ptsMutex;
double audioClock = 0.0; // Current audio clock in seconds
double videoClock = 0.0; // Current video clock in seconds

int audioFreq = 0;
int audioCh = 0;

AVStream* videoStream = nullptr;
AVStream* audioStream = nullptr;
size_t videoBufferOffset = 0;

std::atomic_int numFramesInMemory = 0;

static constexpr double AV_SYNC_THRESHOLD = 0.02;    // Increase to 20ms
static constexpr int FRAME_QUEUE_SIZE = 60;          // Reduce max frame queue size

std::queue<std::pair<AVFrame*, double>> videoFrameQueue;

double getMasterClock() {
    std::lock_guard<std::mutex> lock(ptsMutex);

    if (audioClock > 0) {
        return audioClock; // Use audio as the master
    }

    if (!videoFrameQueue.empty()) {
        return videoFrameQueue.front().second; // Fallback to video clock
    }

    return 0.0; // Default clock
}

// Audio callback function
FMOD_RESULT F_CALLBACK audioCallback(FMOD_SOUND* sound, void* data, unsigned int datalen) {
    std::unique_lock<std::mutex> lock(audioMutex);

    // Wait with timeout to prevent deadlock
    if (!audioCv.wait_for(lock, std::chrono::milliseconds(10),
        [] { return !audioBufferQueue.empty(); })) {
        // If timeout, fill with silence
        std::memset(data, 0, datalen);
        return FMOD_OK;
    }

    int copyLen = std::min(static_cast<unsigned int>(audioBufferQueue.size()), datalen);
    std::copy(audioBufferQueue.begin(), audioBufferQueue.begin() + copyLen, (uint8_t*)data);
    audioBufferQueue.erase(audioBufferQueue.begin(), audioBufferQueue.begin() + copyLen);

    // Fill remaining buffer with silence if we don't have enough data
    if (copyLen < datalen) {
        std::memset((uint8_t*)data + copyLen, 0, datalen - copyLen);
    }

    // Update audio clock
    std::lock_guard<std::mutex> ptsLock(ptsMutex);
    audioClock += static_cast<double>(copyLen) / (audioFreq * audioCh * 2);

    return FMOD_OK;
}

void VideoRenderer::RenderThisFrameToScreen(AVFrame* videoFrame, const std::shared_ptr<donut::engine::FramebufferFactory>& framebufferFactory, nvrhi::CommandListHandle commandList)
{
    // Ensure texture is valid
    nvrhi::TextureHandle texture = m_dynamicYUVSource;
    if (!texture || !videoFrame)
    {
        av_frame_free(&videoFrame);
        return;
    }

    // Ensure the texture description matches the frame dimensions
    const nvrhi::TextureDesc& desc = texture->getDesc();
    // Validate texture format - height should be 1.5x original height for proper YUV420p packing
    if (desc.width != videoFrame->width || desc.height != (videoFrame->height * 3) / 2 || desc.format != nvrhi::Format::R8_UNORM)
    {
        av_frame_free(&videoFrame);
        return;
    }

    size_t rowPitch = desc.width;
    size_t yPlaneSize = videoFrame->height * rowPitch;
    size_t uvPlaneSize = (yPlaneSize / 4); // Each chroma plane is 1/4 of Y

    std::vector<uint8_t> combinedData(yPlaneSize + 2 * uvPlaneSize); // Total size for Y + U + V

    // YUV interleaving
    // Copy Y plane
    for (int y = 0; y < videoFrame->height; ++y)
    {
        std::memcpy(
            &combinedData[y * rowPitch],
            videoFrame->data[0] + y * videoFrame->linesize[0],
            rowPitch
        );
    }

    // Copy U plane
    uint8_t* uPlaneStart = combinedData.data() + yPlaneSize;
    for (int y = 0; y < videoFrame->height / 2; ++y)
    {
        // interleave this bitch
        std::memcpy(
            uPlaneStart + y * (rowPitch / 2),
            videoFrame->data[1] + y * videoFrame->linesize[1],
            rowPitch / 2
        );
    }

    // Copy V plane
    uint8_t* vPlaneStart = uPlaneStart + uvPlaneSize;
    for (int y = 0; y < videoFrame->height / 2; ++y)
    {
        // interleave this bitch 2
        std::memcpy(
            vPlaneStart + y * (rowPitch / 2),
            videoFrame->data[2] + y * videoFrame->linesize[2],
            rowPitch / 2
        );
    }

    // Clean up the frame's memory
    av_frame_free(&videoFrame);

    commandList->writeTexture(
        texture,
        0,
        0,
        combinedData.data(),
        rowPitch,
        0
    );

    // Decrement the frame memory counter
    numFramesInMemory--;
    videoFrameQueue.pop();
}

void decodeVideoFrame(AVPacket* packet, AVCodecContext* videoCodecCtx) {
    AVFrame* frame = av_frame_alloc();
    if (avcodec_send_packet(videoCodecCtx, packet) == 0) {
        while (avcodec_receive_frame(videoCodecCtx, frame) == 0) {
            if (!frame->data[0]) {
                av_frame_unref(frame);
                continue;
            }

            // Use more precise timestamp calculation
            double pts;
            if (frame->pts != AV_NOPTS_VALUE) {
                pts = frame->pts * av_q2d(videoStream->time_base);
            }
            else {
                pts = frame->best_effort_timestamp * av_q2d(videoStream->time_base);
            }

            AVFrame* frameCopy = av_frame_clone(frame);

            if (numFramesInMemory < FRAME_QUEUE_SIZE) {
                videoFrameQueue.push(std::make_pair(frameCopy, pts));
                numFramesInMemory++;
            }
            else {
                av_frame_free(&frameCopy);
            }

            av_frame_unref(frame);
        }
    }
    av_frame_free(&frame);
}

void decodeAudioFrame(AVPacket* packet, AVCodecContext* audioCodecCtx, SwrContext* swrCtx, int outChannels, int outSampleRate) {
    AVFrame* frame = av_frame_alloc();

    if (avcodec_send_packet(audioCodecCtx, packet) == 0) {
        while (avcodec_receive_frame(audioCodecCtx, frame) == 0) {
            int64_t dstNbSamples = av_rescale_rnd(frame->nb_samples, outSampleRate, frame->sample_rate, AV_ROUND_UP);
            std::vector<uint8_t> audioBuffer(dstNbSamples * outChannels * av_get_bytes_per_sample(AV_SAMPLE_FMT_S16));

            uint8_t* outBuffer[] = { audioBuffer.data() };
            int convertedSamples = swr_convert(swrCtx, outBuffer, dstNbSamples,
                (const uint8_t**)frame->data, frame->nb_samples);

            std::lock_guard<std::mutex> lock(audioMutex);
            audioBufferQueue.insert(audioBufferQueue.end(), audioBuffer.begin(), audioBuffer.end());
            audioCv.notify_one();
        }
    }

    av_frame_free(&frame);
}

// Callback function for AVIOContext to read data from memory
static int read_buffer(void* opaque, uint8_t* buf, int buf_size) {
    // 'opaque' is the pointer to the data structure we use to manage the memory buffer.

    auto* context = static_cast<BufferContext*>(opaque);

    if (context->position >= context->size) {
        return AVERROR_EOF; // End of the buffer
    }

    int remaining = context->size - context->position;
    int to_copy = std::min(buf_size, remaining);

    // Copy the data to the buffer
    memcpy(buf, context->data + context->position, to_copy);
    context->position += to_copy;

    return to_copy;
}

VideoRenderer::VideoRenderer(nvrhi::DeviceHandle device, std::shared_ptr<donut::vfs::IFileSystem> filesystem, std::string videoPath)
{
    // Read the file into memory
    auto fileBlob = filesystem->readFile(videoPath);

    auto data = new uint8_t[fileBlob->size()];
    memcpy_s(data, fileBlob->size(), fileBlob->data(), fileBlob->size());

    // Initialize the buffer context
    bufferContext = new BufferContext{
        .data = data,
        .size = fileBlob->size(),
        .position = 0
    };

    formatCtx = avformat_alloc_context();
    if (!formatCtx) {
        delete[] data;
        delete bufferContext;
        throw std::runtime_error("Could not allocate format context.");
    }

    // Allocate AVIOContext
    avioBuffer = static_cast<uint8_t*>(av_malloc(4096));
    if (!avioBuffer) {
        avformat_free_context(formatCtx);
        delete[] data;
        delete bufferContext;
        throw std::runtime_error("Could not allocate AVIOContext buffer.");
    }

    avioCtx = avio_alloc_context(
        avioBuffer, 4096, 0, bufferContext, read_buffer, nullptr, nullptr);
    if (!avioCtx) {
        av_free(avioBuffer);
        avformat_free_context(formatCtx);
        delete[] data;
        delete bufferContext;
        throw std::runtime_error("Could not allocate AVIOContext.");
    }

    formatCtx->pb = avioCtx;

    // Open the input from the memory stream
    if (avformat_open_input(&formatCtx, nullptr, nullptr, nullptr) < 0) {
        av_freep(&avioCtx->buffer);
        av_freep(&formatCtx->pb);
        avformat_free_context(formatCtx);
        delete[] data;
        delete bufferContext;
        throw std::runtime_error("Could not open input from memory.");
    }

    // Retrieve stream information
    if (avformat_find_stream_info(formatCtx, nullptr) < 0) {
        avformat_close_input(&formatCtx);
        throw std::runtime_error("Could not find stream info.");
    }

    // Find video and audio streams
    videoStreamIndex = -1, audioStreamIndex = -1;
    AVCodecParameters* videoCodecParams = nullptr;
    AVCodecParameters* audioCodecParams = nullptr;

    for (unsigned i = 0; i < formatCtx->nb_streams; ++i) {
        if (formatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            videoStreamIndex = i;
            videoCodecParams = formatCtx->streams[i]->codecpar;
            videoStream = formatCtx->streams[i];
        }
        else if (formatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            audioStreamIndex = i;
            audioCodecParams = formatCtx->streams[i]->codecpar;
            audioStream = formatCtx->streams[i];
        }
    }

    if (videoStreamIndex == -1 || audioStreamIndex == -1) {
        throw std::runtime_error("Could not find video or audio stream.");
    }

    // Video codec setup
    const AVCodec* videoCodec = avcodec_find_decoder(videoCodecParams->codec_id);
    videoCodecCtx = avcodec_alloc_context3(videoCodec);
    avcodec_parameters_to_context(videoCodecCtx, videoCodecParams);

    videoCodecCtx->thread_count = 16;
    videoCodecCtx->thread_type = FF_THREAD_FRAME;

    if (avcodec_open2(videoCodecCtx, videoCodec, nullptr) < 0) {
        throw std::runtime_error("Failed to open video codec.");
    }

    // Audio codec setup
    const AVCodec* audioCodec = avcodec_find_decoder(audioCodecParams->codec_id);
    audioCodecCtx = avcodec_alloc_context3(audioCodec);
    avcodec_parameters_to_context(audioCodecCtx, audioCodecParams);

    if (avcodec_open2(audioCodecCtx, audioCodec, nullptr) < 0) {
        throw std::runtime_error("Failed to open audio codec.");
    }

    swrCtx = swr_alloc();
    av_opt_set_chlayout(swrCtx, "in_channel_layout", &audioCodecParams->ch_layout, 0);
    av_opt_set_chlayout(swrCtx, "out_channel_layout", &audioCodecParams->ch_layout, 0);
    av_opt_set_int(swrCtx, "in_sample_rate", audioCodecCtx->sample_rate, 0);
    av_opt_set_int(swrCtx, "out_sample_rate", audioCodecCtx->sample_rate, 0);
    av_opt_set_sample_fmt(swrCtx, "in_sample_fmt", audioCodecCtx->sample_fmt, 0);
    av_opt_set_sample_fmt(swrCtx, "out_sample_fmt", AV_SAMPLE_FMT_S16, 0);

    if (swr_init(swrCtx) < 0) {
        throw std::runtime_error("Failed to initialize SwrContext.");
    }

    packet = av_packet_alloc();
    EOV = false;

    audioFreq = audioCodecCtx->sample_rate;
    audioCh = audioCodecCtx->channels;

    framerate = av_q2d(videoStream->avg_frame_rate);

    double duration = audioStream->duration * av_q2d(audioStream->time_base); // in AV_TIME_BASE units

    m_streamedSound = AudioEngine::LoadStreamedSound(audioFreq, audioCh, duration, audioCallback);

    audioStarted = false;

    // intermediate texture for fxaa
    nvrhi::TextureDesc texDesc;

    texDesc.format = nvrhi::Format::R8_UNORM; // YUV raw data
    texDesc.width = videoCodecParams->width;
    texDesc.height = (videoCodecParams->height * 3) / 2;
    texDesc.mipLevels = 1;
    texDesc.isRenderTarget = false;
    texDesc.isShaderResource = true;
    texDesc.initialState = nvrhi::ResourceStates::ShaderResource;
    texDesc.keepInitialState = true;
    texDesc.arraySize = 1;
    texDesc.sampleCount = 1;
    texDesc.sampleQuality = 0;

    texDesc.debugName = "YUV shader resource";
    m_dynamicYUVSource = device->createTexture(texDesc);

    while (numFramesInMemory < FRAME_QUEUE_SIZE && !EOV) {
        if (av_read_frame(formatCtx, packet) >= 0) {
            if (packet->stream_index == audioStreamIndex) {
                decodeAudioFrame(packet, audioCodecCtx, swrCtx, 2, audioCodecCtx->sample_rate);
            }
            if (packet->stream_index == videoStreamIndex) {
                decodeVideoFrame(packet, videoCodecCtx);
            }
            av_packet_unref(packet);
        }
        else {
            if (numFramesInMemory == 0)
                EOV = true;
        }
    }
}

void VideoRenderer::PresentFrame(const std::shared_ptr<donut::engine::FramebufferFactory>& framebufferFactory,
    nvrhi::CommandListHandle commandList) {

    static bool performedPreBuffering = false;

    //  frame decoding
    if (numFramesInMemory < FRAME_QUEUE_SIZE && !EOV) {
        if (av_read_frame(formatCtx, packet) >= 0) {
            if (packet->stream_index == audioStreamIndex) {
                decodeAudioFrame(packet, audioCodecCtx, swrCtx, 2, audioCodecCtx->sample_rate);
            }
            if (packet->stream_index == videoStreamIndex) {
                decodeVideoFrame(packet, videoCodecCtx);
            }
            av_packet_unref(packet);
        }
        else {
            if(numFramesInMemory == 0)
                EOV = true;        
        }
    }

    if (!VideoRenderer::audioStarted)
    {
        VideoRenderer::audioStarted = true;
        auto res = AudioEngine::m_system->playSound(m_streamedSound, nullptr, false, &m_streamedSoundChannel);
        VideoRenderer::m_streamedSoundChannel->setVolume(1.0f);
    }

    if (numFramesInMemory == 0)
        return;

    static auto lastFrameTime = std::chrono::high_resolution_clock::now();
    auto currentTime = std::chrono::high_resolution_clock::now();
    double deltaTimeInSeconds = std::chrono::duration<double>(currentTime - lastFrameTime).count();
    lastFrameTime = currentTime;

    auto& currentFrame = videoFrameQueue.front();
    double pts = currentFrame.second;

    double masterClock = getMasterClock();
    double diff = pts - masterClock;
    double frameDelay = 1.0 / framerate;

    // Adjust sync threshold based on frame delay
    double syncThreshold = std::max(AV_SYNC_THRESHOLD, frameDelay * 0.5);

    if (diff <= -syncThreshold) {
        // Drop late frame
        AVFrame* frame = currentFrame.first;
        av_frame_free(&frame); // dont forget to free it dum dum
        videoFrameQueue.pop();
        numFramesInMemory--;
        return;
    }

    // Render the frame
    RenderThisFrameToScreen(currentFrame.first, framebufferFactory, commandList);

    auto delay = std::chrono::milliseconds(static_cast<int>(800.0 / framerate));
    std::this_thread::sleep_for(delay);
}

//void VideoRenderer::UninitFFMPEG()
//{
//    m_streamedSoundChannel->stop();
//
//    if (m_streamedSound)
//    {
//        m_streamedSound->release();
//    }
//
//    av_packet_free(&packet);
//    while (!videoFrameQueue.empty())
//    {
//        auto& frame = videoFrameQueue.front().first;
//        av_frame_free(&frame);
//        videoFrameQueue.pop();
//    }
//    while (!audioBufferQueue.empty())
//        audioBufferQueue.clear();
//    swr_free(&swrCtx);
//    delete bufferContext;
//    avcodec_free_context(&videoCodecCtx);
//    avcodec_free_context(&audioCodecCtx);
//    avformat_close_input(&formatCtx);
//    avio_context_free(&avioCtx);
//}