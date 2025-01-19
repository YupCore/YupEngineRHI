#pragma once
#include <donut/engine/SceneGraph.h>
#include <donut/core/vfs/VFS.h>
#include <fmod.hpp>

class AudioEngine
{
public:
    static void InitEngine(std::shared_ptr<donut::vfs::IFileSystem> filesystem);
    static void UninitEngine();
    static FMOD::Sound* LoadSound(std::string soundPath, unsigned int mode);
    static FMOD::Sound* LoadStreamedSound(int freq, int channels, double lengthInSecs, FMOD_SOUND_PCMREAD_CALLBACK readdataCallback);

    static void SetListenerAttributes(const dm::float3& position, const dm::float3& forward, const dm::float3& up);

    static FMOD::System* m_system;

private:
    static bool engineInit;
    static std::shared_ptr<donut::vfs::IFileSystem> m_fs;
};
