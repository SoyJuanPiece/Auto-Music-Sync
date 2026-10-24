#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>

using namespace geode::prelude;

// OBJECT IDS | slow = 200 0.7 | normal speed = 201, 0.9 | 2x = 202 1.1 | 3x = 203 1.3 | 4x = 1334 1.6

class $modify(ResetMusicLayer, PlayLayer) {

    struct Fields {
        float pitch = 1.0f;
        float supposedSpeed = 1.0f;
        bool needingOfPitch = false;
        // Per-level speed portal positions (X coordinate where portal triggers)
        std::vector<int> slowSpeed;
        std::vector<int> normalSpeed;
        std::vector<int> fastSpeed;
        std::vector<int> fasterSpeed;
        std::vector<int> fastestSpeed;
        bool portalsLoaded = false;
    };

    bool init(GJGameLevel *level, bool useReplay, bool dontCreateObjects) {
        bool result = PlayLayer::init(level, useReplay, dontCreateObjects);
        m_fields->supposedSpeed = 1.0f;
        m_fields->pitch = 1.0f;
        m_fields->needingOfPitch = false;
        m_fields->portalsLoaded = false;
        m_fields->slowSpeed.clear();
        m_fields->normalSpeed.clear();
        m_fields->fastSpeed.clear();
        m_fields->fasterSpeed.clear();
        m_fields->fastestSpeed.clear();

        auto fmod = FMODAudioEngine::sharedEngine();
        FMOD::ChannelGroup* mGroup = nullptr;
        if (fmod && fmod->m_system->getMasterChannelGroup(&mGroup) == FMOD_OK && mGroup) {
            mGroup->setPitch(1.0f);
        }

        // Reset pitch state so we start fresh
        quickRestart();

        gd::string levelString = m_level->m_levelString;
        std::string decompString = ZipUtils::decompressString(levelString, true, 0);
        std::vector<std::string> leveldata = splitString(decompString, ';');

        int pixelBuffer = Mod::get()->getSettingValue<int64_t>("pixelBuffer");

        for (int i = 0; i < static_cast<int>(leveldata.size()); i++) {
            auto& entry = leveldata[i];
            if (entry.substr(0, 6) == "1,200,") {
                auto parts = splitString(entry, ',');
                if (parts.size() > 3) {
                    m_fields->slowSpeed.push_back(stoi(parts[3]) - pixelBuffer);
                }
            }
            else if (entry.substr(0, 6) == "1,201,") {
                auto parts = splitString(entry, ',');
                if (parts.size() > 3) {
                    m_fields->normalSpeed.push_back(stoi(parts[3]) - pixelBuffer);
                }
            }
            else if (entry.substr(0, 6) == "1,202,") {
                auto parts = splitString(entry, ',');
                if (parts.size() > 3) {
                    m_fields->fastSpeed.push_back(stoi(parts[3]) - pixelBuffer);
                }
            }
            else if (entry.substr(0, 6) == "1,203,") {
                auto parts = splitString(entry, ',');
                if (parts.size() > 3) {
                    m_fields->fasterSpeed.push_back(stoi(parts[3]) - pixelBuffer);
                }
            }
            else if (entry.substr(0, 7) == "1,1334,") {
                auto parts = splitString(entry, ',');
                if (parts.size() > 3) {
                    m_fields->fastestSpeed.push_back(stoi(parts[3]) - pixelBuffer);
                }
            }
        }

        cleanVector(m_fields->slowSpeed);
        cleanVector(m_fields->normalSpeed);
        cleanVector(m_fields->fastSpeed);
        cleanVector(m_fields->fasterSpeed);
        cleanVector(m_fields->fastestSpeed);

        m_fields->portalsLoaded = true;
        return result;
    }

    void postUpdate(float p0) {
        PlayLayer::postUpdate(p0);

        if (!m_player1 || !m_fields->portalsLoaded) return;

        auto fmod = FMODAudioEngine::sharedEngine();
        if (!fmod) return;

        FMOD::ChannelGroup* mGroup = nullptr;
        if (fmod->m_system->getMasterChannelGroup(&mGroup) != FMOD_OK || !mGroup) return;

        // Initialize supposedSpeed on first valid frame
        if (m_fields->supposedSpeed == 0.0f) {
            m_fields->supposedSpeed = m_player1->m_playerSpeed;
            if (m_fields->supposedSpeed == 0.0f) m_fields->supposedSpeed = 1.0f;
        }

        float currentSpeed = m_player1->m_playerSpeed;
        float playerX = floor(m_player1->getPositionX());
        float newSupposedSpeed = m_fields->supposedSpeed;
        bool foundPortal = false;

        // Check slow portals (0.7x)
        for (int pos : m_fields->slowSpeed) {
            if (std::abs(playerX - pos) <= 2.0f) {
                newSupposedSpeed = 0.7f;
                foundPortal = true;
                break;
            }
        }
        // Check normal portals (0.9x)
        if (!foundPortal) {
            for (int pos : m_fields->normalSpeed) {
                if (std::abs(playerX - pos) <= 2.0f) {
                    newSupposedSpeed = 0.9f;
                    foundPortal = true;
                    break;
                }
            }
        }
        // Check fast portals (1.1x)
        if (!foundPortal) {
            for (int pos : m_fields->fastSpeed) {
                if (std::abs(playerX - pos) <= 2.0f) {
                    newSupposedSpeed = 1.1f;
                    foundPortal = true;
                    break;
                }
            }
        }
        // Check faster portals (1.3x)
        if (!foundPortal) {
            for (int pos : m_fields->fasterSpeed) {
                if (std::abs(playerX - pos) <= 2.0f) {
                    newSupposedSpeed = 1.3f;
                    foundPortal = true;
                    break;
                }
            }
        }
        // Check fastest portals (1.6x)
        if (!foundPortal) {
            for (int pos : m_fields->fastestSpeed) {
                if (std::abs(playerX - pos) <= 2.0f) {
                    newSupposedSpeed = 1.6f;
                    foundPortal = true;
                    break;
                }
            }
        }

        // If we detected a new speed target, update it
        if (foundPortal && newSupposedSpeed != m_fields->supposedSpeed) {
            m_fields->supposedSpeed = newSupposedSpeed;
        }

        if (m_fields->needingOfPitch) {
            if (!m_player1->m_isPlatformer) {
                if (currentSpeed != m_fields->supposedSpeed && m_fields->supposedSpeed != 0.0f) {
                    // Adjust pitch to compensate for speed difference
                    // pitch = currentSpeed / supposedSpeed keeps music aligned with gameplay
                    m_fields->pitch = currentSpeed / m_fields->supposedSpeed;
                    // Clamp pitch to sane range to avoid extreme audio distortion
                    if (m_fields->pitch < 0.1f) m_fields->pitch = 0.1f;
                    if (m_fields->pitch > 10.0f) m_fields->pitch = 10.0f;
                    mGroup->setPitch(m_fields->pitch);
                }
                else if (currentSpeed == m_fields->supposedSpeed) {
                    mGroup->setPitch(1.0f);
                    m_fields->pitch = 1.0f;
                }
            }
        }
    }

    void updateAttempts() {
        PlayLayer::updateAttempts();
        resetMusic();
    }

    void onExit() {
        PlayLayer::onExit();
        resetMusic();
    }

    void resetLevel() {
        PlayLayer::resetLevel();
        resetMusic();
        quickRestart();
    }

    void levelComplete() {
        PlayLayer::levelComplete();
        resetMusic();
    }

    void resetMusic() {
        m_fields->needingOfPitch = false;
        m_fields->supposedSpeed = 1.0f;
        m_fields->pitch = 1.0f;
        auto fmod = FMODAudioEngine::sharedEngine();
        if (!fmod) return;
        FMOD::ChannelGroup* mGroup = nullptr;
        if (fmod->m_system->getMasterChannelGroup(&mGroup) == FMOD_OK && mGroup) {
            mGroup->setPitch(1.0f);
        }
    }

    void quickRestart() {
        m_fields->supposedSpeed = 1.0f;
        m_fields->pitch = 1.0f;
        m_fields->needingOfPitch = false;
        auto fmod = FMODAudioEngine::sharedEngine();
        if (!fmod) return;
        FMOD::ChannelGroup* mGroup = nullptr;
        if (fmod->m_system->getMasterChannelGroup(&mGroup) == FMOD_OK && mGroup) {
            mGroup->setPitch(1.0f);
        }
        m_fields->needingOfPitch = true;
    }

private:
    std::vector<std::string> splitString(const std::string& s, char delimiter) {
        std::vector<std::string> tokens;
        std::istringstream ss(s);
        std::string token;
        while (std::getline(ss, token, delimiter)) {
            tokens.push_back(token);
        }
        return tokens;
    }

    void cleanVector(std::vector<int>& vector) {
        std::sort(vector.begin(), vector.end());
        auto last = std::unique(vector.begin(), vector.end());
        vector.erase(last, vector.end());
    }
};
