#include "cli_launcher/ConfigManager.hpp"
#include <fstream>
#include <sstream>
#include <mutex>

namespace cccaster::main_app {

std::unordered_map<std::string, std::unordered_map<std::string, std::string>> ConfigManager::configData;
std::shared_mutex ConfigManager::mutex_;

static inline std::string Trim(const std::string& s) {
    if (s.empty()) return s;
    size_t first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    size_t last = s.find_last_not_of(" \t\r\n");
    return s.substr(first, (last - first + 1));
}

// --- Config Implementation ---

void Config::Load(const std::string& filePath) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    configData.clear();
    std::ifstream file(filePath);
    if (!file.is_open()) return;

    std::string line;
    std::string currentSection = "";

    while (std::getline(file, line)) {
        line = Trim(line);
        if (line.empty() || line[0] == ';' || line[0] == '#') continue;

        if (line[0] == '[' && line[line.length() - 1] == ']') {
            currentSection = line.substr(1, line.length() - 2);
        } else {
            size_t delimPos = line.find('=');
            if (delimPos != std::string::npos) {
                std::string key = Trim(line.substr(0, delimPos));
                std::string value = Trim(line.substr(delimPos + 1));
                configData[currentSection][key] = value;
            }
        }
    }
}

void Config::Save(const std::string& filePath) {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    std::ofstream file(filePath);
    if (!file.is_open()) return;

    for (const auto& sectionPair : configData) {
        if (!sectionPair.first.empty()) {
            file << "[" << sectionPair.first << "]\n";
        }
        for (const auto& keyValuePair : sectionPair.second) {
            file << keyValuePair.first << " = " << keyValuePair.second << "\n";
        }
        file << "\n";
    }
}

void Config::Clear() {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    configData.clear();
}

std::string Config::GetString(const std::string& section, const std::string& key, const std::string& defaultValue) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    auto secIt = configData.find(section);
    if (secIt != configData.end()) {
        auto keyIt = secIt->second.find(key);
        if (keyIt != secIt->second.end()) {
            return keyIt->second;
        }
    }
    return defaultValue;
}

int Config::GetInt(const std::string& section, const std::string& key, int defaultValue) const {
    std::string strVal = GetString(section, key, "");
    if (!strVal.empty()) {
        try {
            return std::stoi(strVal);
        } catch (...) {
            return defaultValue;
        }
    }
    return defaultValue;
}

void Config::SetString(const std::string& section, const std::string& key, const std::string& value) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    configData[section][key] = value;
}

void Config::SetInt(const std::string& section, const std::string& key, int value) {
    SetString(section, key, std::to_string(value));
}

// --- ConfigManager Implementation ---


void ConfigManager::Load(const std::string& filePath) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    configData.clear();
    std::ifstream file(filePath);
    if (!file.is_open()) return;

    std::string line;
    std::string currentSection = "";

    while (std::getline(file, line)) {
        line = Trim(line);
        if (line.empty() || line[0] == ';' || line[0] == '#') continue;

        if (line[0] == '[' && line[line.length() - 1] == ']') {
            currentSection = line.substr(1, line.length() - 2);
        } else {
            size_t delimPos = line.find('=');
            if (delimPos != std::string::npos) {
                std::string key = Trim(line.substr(0, delimPos));
                std::string value = Trim(line.substr(delimPos + 1));
                configData[currentSection][key] = value;
            }
        }
    }
}

void ConfigManager::Save(const std::string& filePath) {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    std::ofstream file(filePath);
    if (!file.is_open()) return;

    for (const auto& sectionPair : configData) {
        if (!sectionPair.first.empty()) {
            file << "[" << sectionPair.first << "]\n";
        }
        for (const auto& keyValuePair : sectionPair.second) {
            file << keyValuePair.first << " = " << keyValuePair.second << "\n";
        }
        file << "\n";
    }
}

std::string ConfigManager::GetString(const std::string& section, const std::string& key, const std::string& defaultValue) {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    auto secIt = configData.find(section);
    if (secIt != configData.end()) {
        auto keyIt = secIt->second.find(key);
        if (keyIt != secIt->second.end()) {
            return keyIt->second;
        }
    }
    return defaultValue;
}

int ConfigManager::GetInt(const std::string& section, const std::string& key, int defaultValue) {
    std::string strVal = GetString(section, key, "");
    if (!strVal.empty()) {
        try {
            return std::stoi(strVal);
        } catch (...) {
            return defaultValue;
        }
    }
    return defaultValue;
}

void ConfigManager::SetString(const std::string& section, const std::string& key, const std::string& value) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    configData[section][key] = value;
}

void ConfigManager::SetInt(const std::string& section, const std::string& key, int value) {
    SetString(section, key, std::to_string(value));
}

} // namespace cccaster::main_app
