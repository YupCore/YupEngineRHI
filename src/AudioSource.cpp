#include "AudioSource.h"

AudioSource::AudioSource(std::string name, float vol, bool loop, bool is2D, float min3dDist, float max3dDist) :
    sound_path(name), m_volume(vol), m_channel(nullptr)
{
    unsigned int mode = (is2D ? FMOD_2D : FMOD_3D) | (loop ? FMOD_LOOP_NORMAL : FMOD_LOOP_OFF);
    m_sound = AudioEngine::LoadSound(name, mode);
    m_sound->set3DMinMaxDistance(min3dDist, max3dDist);
    if (!m_sound)
    {
        donut::log::error("Failed to load sound: {}", name);
        return;
    }

    SetVolume(vol);
}

AudioSource::AudioSource(FMOD::Sound* sound, float vol, bool is2D, float min3dDist, float max3dDist) :
    m_volume(vol), m_channel(nullptr)
{
    unsigned int mode = (is2D ? FMOD_2D : FMOD_3D);
    m_sound = sound;
    m_sound->set3DMinMaxDistance(min3dDist, max3dDist);
    SetVolume(vol);
}


void AudioSource::Play()
{
    SetName(std::filesystem::path(sound_path).filename().generic_string());

    if (!m_sound)
    {
        donut::log::error("No sound loaded to play.");
        return;
    }

    FMOD_RESULT result = AudioEngine::m_system->playSound(m_sound, nullptr, false, &m_channel);
    if (result == FMOD_OK && m_channel)
    {
        Update3DAttributes(); // Set initial 3D attributes
    }
    else
    {
        donut::log::error("FMOD Error: Failed to play sound: {}", sound_path);
    }
}

void AudioSource::Stop()
{
    if (m_channel)
    {
        m_channel->stop();
    }
}

void AudioSource::SetVolume(float volume)
{
    m_volume = volume;
    if (m_channel)
    {
        m_channel->setVolume(volume);
    }
}

void AudioSource::Update3DAttributes()
{
    auto node = GetNode();
    if (node)
    {
        auto transform = node->GetLocalToWorldTransform();
        dm::double3 position = transform.m_translation;
        auto velocity = transform.m_translation - node->GetPrevLocalToWorldTransform().m_translation;

        if (m_channel)
        {
            FMOD_VECTOR pos = { static_cast<float>(position.x), static_cast<float>(position.y), static_cast<float>(position.z) };
            FMOD_VECTOR vel = { static_cast<float>(velocity.x), static_cast<float>(velocity.y), static_cast<float>(velocity.z) }; // Placeholder velocity
            m_channel->set3DAttributes(&pos, &vel);
        }
    }
}

AudioSource::~AudioSource()
{
    if (m_sound)
    {
        m_sound->release();
    }
}

std::shared_ptr<donut::engine::SceneGraphLeaf> AudioSource::Clone()
{
    return std::make_shared<AudioSource>(sound_path, m_volume);
}
