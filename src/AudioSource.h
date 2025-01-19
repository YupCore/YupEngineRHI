#pragma once

#include "AudioEngine.h"
#include <donut/core/log.h>

class AudioSource : public donut::engine::SceneGraphLeaf
{
public:
    AudioSource(std::string name, float vol = 1.0f, bool loop = false, bool is2D = true, float min3dDist = 0.1f, float max3dDist = 100.f);
    AudioSource(FMOD::Sound* sound, float vol = 1.0f, bool is2D = true, float min3dDist = 0.1f, float max3dDist = 100.f);

    void Play();
    void Stop();
    void SetVolume(float volume);
    void Update3DAttributes(); // Update 3D position, velocity, etc.

    ~AudioSource();

    std::string sound_path;

    std::shared_ptr<donut::engine::SceneGraphLeaf> Clone() override;

    [[nodiscard]] dm::box3 GetLocalBoundingBox() override { return dm::box3::empty(); }

private:
    float m_volume;
    FMOD::Sound* m_sound;
    FMOD::Channel* m_channel; // FMOD channel for playback control
};
