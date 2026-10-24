#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>

using namespace geode::prelude;

// OBJECT IDS | slow = 200 (0.7x) | normal = 201 (0.9x) | 2x = 202 (1.1x) | 3x = 203 (1.3x) | 4x = 1334 (1.6x)

class $modify(ResetMusicLayer, PlayLayer) {

    struct Fields {
        float currentPitch = 1.0f;   // pitch actual suavizado
        float targetPitch = 1.0f;    // pitch al que queremos llegar
        float supposedSpeed = 1.0f;  // velocidad objetivo del nivel
        bool needingOfPitch = false;
        bool portalsLoaded = false;
        // Per-level speed portal positions (X coordinate where portal triggers)
        std::vector<int> slowSpeed;
        std::vector<int> normalSpeed;
        std::vector<int> fastSpeed;
        std::vector<int> fasterSpeed;
        std::vector<int> fastestSpeed;
        int activePortalType = 0; // 0=none, 1=slow, 2=normal, 3=fast, 4=faster, 5=fastest
        // Hysteresis: extra margin when leaving a portal to prevent flickering
        int leavePortalType = 0;
    };

    bool init(GJGameLevel *level, bool useReplay, bool dontCreateObjects) {
        bool result = PlayLayer::init(level, useReplay, dontCreateObjects);
        m_fields->supposedSpeed = 1.0f;
        m_fields->currentPitch = 1.0f;
        m_fields->targetPitch = 1.0f;
        m_fields->needingOfPitch = false;
        m_fields->portalsLoaded = false;
        m_fields->activePortalType = 0;
        m_fields->leavePortalType = 0;
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

        // Initialize on first valid frame
        if (!m_fields->needingOfPitch) {
            m_fields->supposedSpeed = m_player1->m_playerSpeed;
            if (m_fields->supposedSpeed == 0.0f) m_fields->supposedSpeed = 1.0f;
            m_fields->currentPitch = m_fields->supposedSpeed;
            m_fields->targetPitch = m_fields->supposedSpeed;
            mGroup->setPitch(m_fields->currentPitch);
            m_fields->needingOfPitch = true;
        }

        float currentSpeed = m_player1->m_playerSpeed;
        float playerX = floor(m_player1->getPositionX());

        // Detect which speed portal we are currently at
        int newPortalType = 0;
        float targetSpeed = 1.0f;

        for (int pos : m_fields->slowSpeed) {
            if (std::abs(playerX - pos) <= 3.0f) { newPortalType = 1; targetSpeed = 0.7f; break; }
        }
        if (newPortalType == 0) {
            for (int pos : m_fields->normalSpeed) {
                if (std::abs(playerX - pos) <= 3.0f) { newPortalType = 2; targetSpeed = 0.9f; break; }
            }
        }
        if (newPortalType == 0) {
            for (int pos : m_fields->fastSpeed) {
                if (std::abs(playerX - pos) <= 3.0f) { newPortalType = 3; targetSpeed = 1.1f; break; }
            }
        }
        if (newPortalType == 0) {
            for (int pos : m_fields->fasterSpeed) {
                if (std::abs(playerX - pos) <= 3.0f) { newPortalType = 4; targetSpeed = 1.3f; break; }
            }
        }
        if (newPortalType == 0) {
            for (int pos : m_fields->fastestSpeed) {
                if (std::abs(playerX - pos) <= 3.0f) { newPortalType = 5; targetSpeed = 1.6f; break; }
            }
        }

        // Enter a portal: set target pitch immediately (will be smoothed by lerp below)
        if (newPortalType != 0 && newPortalType != m_fields->activePortalType) {
            m_fields->activePortalType = newPortalType;
            m_fields->leavePortalType = 0;
            m_fields->supposedSpeed = targetSpeed;
            m_fields->targetPitch = targetSpeed;
        }
        // Leave a portal: use hysteresis margin before returning to normal
        else if (newPortalType == 0 && m_fields->activePortalType != 0) {
            if (m_fields->leavePortalType == 0) {
                m_fields->leavePortalType = m_fields->activePortalType;
            }
            // Only return to normal once we've moved far enough away (hysteresis)
            bool farEnough = true;
            // Check distance from the portal we just left
            if (m_fields->leavePortalType == 1) {
                for (int pos : m_fields->slowSpeed) {
                    if (std::abs(playerX - pos) <= 10.0f) { farEnough = false; break; }
                }
            } else if (m_fields->leavePortalType == 2) {
                for (int pos : m_fields->normalSpeed) {
                    if (std::abs(playerX - pos) <= 10.0f) { farEnough = false; break; }
                }
            } else if (m_fields->leavePortalType == 3) {
                for (int pos : m_fields->fastSpeed) {
                    if (std::abs(playerX - pos) <= 10.0f) { farEnough = false; break; }
                }
            } else if (m_fields->leavePortalType == 4) {
                for (int pos : m_fields->fasterSpeed) {
                    if (std::abs(playerX - pos) <= 10.0f) { farEnough = false; break; }
                }
            } else if (m_fields->leavePortalType == 5) {
                for (int pos : m_fields->fastestSpeed) {
                    if (std::abs(playerX - pos) <= 10.0f) { farEnough = false; break; }
                }
            }

            if (farEnough) {
                m_fields->activePortalType = 0;
                m_fields->leavePortalType = 0;
                m_fields->supposedSpeed = 1.0f;
                m_fields->targetPitch = 1.0f;
            }
        }

        // Smoothly interpolate current pitch toward target pitch (prevents audio jumps/pucher)
        float pitchDiff = m_fields->targetPitch - m_fields->currentPitch;
        if (std::abs(pitchDiff) > 0.001f) {
            // Lerp factor: how fast to approach target (per second)
            float lerpSpeed = 8.0f;
            m_fields->currentPitch += pitchDiff * lerpSpeed * p0;
            // Clamp to avoid overshooting
            if (pitchDiff > 0.0f && m_fields->currentPitch > m_fields->targetPitch) {
                m_fields->currentPitch = m_fields->targetPitch;
            }
            if (pitchDiff < 0.0f && m_fields->currentPitch < m_fields->targetPitch) {
                m_fields->currentPitch = m_fields->targetPitch;
            }
            // Safety clamp
            if (m_fields->currentPitch < 0.1f) m_fields->currentPitch = 0.1f;
            if (m_fields->currentPitch > 10.0f) m_fields->currentPitch = 10.0f;
            mGroup->setPitch(m_fields->currentPitch);
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
        m_fields->currentPitch = 1.0f;
        m_fields->targetPitch = 1.0f;
        m_fields->activePortalType = 0;
        m_fields->leavePortalType = 0;
        auto fmod = FMODAudioEngine::sharedEngine();
        if (!fmod) return;
        FMOD::ChannelGroup* mGroup = nullptr;
        if (fmod->m_system->getMasterChannelGroup(&mGroup) == FMOD_OK && mGroup) {
            mGroup->setPitch(1.0f);
        }
    }

    void quickRestart() {
        m_fields->supposedSpeed = 1.0f;
        m_fields->currentPitch = 1.0f;
        m_fields->targetPitch = 1.0f;
        m_fields->activePortalType = 0;
        m_fields->leavePortalType = 0;
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
