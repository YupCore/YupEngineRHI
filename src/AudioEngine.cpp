#include "AudioEngine.h"
#include <donut/core/log.h>
#include <fstream>

FMOD::System* AudioEngine::m_system = nullptr; // FMOD system instance
bool AudioEngine::engineInit = false; // Engine initialization status
std::shared_ptr<donut::vfs::IFileSystem> AudioEngine::m_fs = nullptr; // File system instance

const float DISTANCEFACTOR = 1.0f;          // Units per meter.  I.e feet would = 3.28.  centimeters would = 100.

FMOD_RESULT DebugCallback(FMOD_DEBUG_FLAGS flags, const char* file, int line, const char* func, const char* message)
{
    char buffer[1024]; // Adjust size as necessary for your debug messages
    snprintf(buffer, sizeof(buffer), "[FMOD Debug] %s:%d (%s): %s\n", file, line, func, message);
    donut::log::info(buffer);

    return FMOD_OK;
}

void AudioEngine::InitEngine(std::shared_ptr<donut::vfs::IFileSystem> filesystem)
{
    engineInit = false;

    // Initialize FMOD system
    FMOD_RESULT result = FMOD::System_Create(&m_system);
    if (result != FMOD_OK) {
        donut::log::fatal("FMOD Error: Failed to create FMOD system!");
        return;
    }

    result = m_system->init(512, FMOD_INIT_NORMAL, nullptr); // Initialize the FMOD system with 512 channels
    if (result != FMOD_OK) {
        donut::log::fatal("FMOD Error: Failed to initialize FMOD system!");
        m_system->release();
        return;
    }

    result = m_system->set3DSettings(1.0, DISTANCEFACTOR, 1.0f);

    if (result != FMOD_OK) {
        donut::log::fatal("FMOD Error: Failed to initialize FMOD system!");
        m_system->release();
        return;
    }

#if YUP_ENGINE_BUILD_DEBUG
    result = FMOD::Debug_Initialize(FMOD_DEBUG_LEVEL_LOG, FMOD_DEBUG_MODE_CALLBACK, DebugCallback);

    if (result != FMOD_OK) {
        donut::log::error("FMOD Error: Failed to initialize FMOD debug!");
    }
#endif

    engineInit = true;
    m_fs = filesystem;
}

void AudioEngine::UninitEngine()
{
    if (engineInit)
    {
        // Release FMOD system
        FMOD_RESULT result = m_system->close();
        if (result != FMOD_OK) {
            donut::log::error("FMOD Error: Failed to close FMOD system!");
        }

        result = m_system->release();
        if (result != FMOD_OK) {
            donut::log::error("FMOD Error: Failed to release FMOD system!");
        }

        engineInit = false;
    }
    else
    {
        donut::log::warning("Initialize the engine first!");
    }
}

FMOD::Sound* AudioEngine::LoadSound(std::string soundPath, unsigned int mode)
{
    if (!engineInit)
    {
        donut::log::fatal("Initialize the engine first!");
        return nullptr;
    }

    std::shared_ptr<donut::vfs::IBlob> file = m_fs->readFile(soundPath);
    if (!file || file->size() == 0 || !file->data())
    {
        donut::log::error("Failed to read the sound file: {}", soundPath);
        return nullptr;
    }

    auto fileData = file->data();
    size_t fileSize = file->size();

    FMOD::Sound* sound;

    FMOD_CREATESOUNDEXINFO info;
    memset(&info, 0, sizeof(info));
    info.cbsize = sizeof(info);
    info.length = fileSize;

    FMOD_RESULT result = m_system->createSound(reinterpret_cast<const char*>(fileData), mode | FMOD_OPENMEMORY, &info, &sound);
    if (result != FMOD_OK)
    {
        donut::log::error("FMOD Error: Failed to load sound from memory!");
        return nullptr;
    }

    return sound;
}

FMOD::Sound* AudioEngine::LoadStreamedSound(int freq, int channels, double lengthInSecs, FMOD_SOUND_PCMREAD_CALLBACK readdataCallback)
{
    FMOD_CREATESOUNDEXINFO exinfo;

    memset(&exinfo, 0, sizeof(FMOD_CREATESOUNDEXINFO));

    int approximate = freq * channels * sizeof(signed short) * lengthInSecs;

    exinfo.cbsize = sizeof(FMOD_CREATESOUNDEXINFO);          /* required. */
    exinfo.decodebuffersize = 4096;                          /* Chunk size of stream update in samples.  This will be the amount of data passed to the user callback. */
    exinfo.length = approximate;                             /* Length of PCM data in bytes of whole song (for Sound::getLength) */
    exinfo.numchannels = channels;                           /* Number of channels in the sound. */
    exinfo.defaultfrequency = freq;                          /* Default playback rate of sound. */
    exinfo.format = FMOD_SOUND_FORMAT_PCM16;                 /* Data format of sound. */
    exinfo.pcmreadcallback = readdataCallback;               /* User callback for reading. */
    exinfo.pcmsetposcallback = nullptr;                      /* User callback for seeking. */

    FMOD::Sound* sound;
    FMOD_RESULT result = m_system->createStream(NULL, FMOD_2D | FMOD_LOOP_OFF | FMOD_OPENUSER | FMOD_OPENONLY | FMOD_OPENRAW, &exinfo, &sound);
    if (result != FMOD_OK)
    {
        donut::log::error("FMOD Error: Failed to load sound from stream!");
        return nullptr;
    }

    return sound;
}

void AudioEngine::SetListenerAttributes(const dm::float3& position, const dm::float3& forward, const dm::float3& up)
{
    if (!engineInit)
    {
        donut::log::error("Engine not initialized!");
        return;
    }

    FMOD_VECTOR pos = { position.x, position.y, position.z };
    FMOD_VECTOR fwd = { forward.x, forward.y, forward.z };
    FMOD_VECTOR upv = { up.x, up.y, up.z };

    m_system->set3DListenerAttributes(0, &pos, nullptr, &fwd, &upv);
}