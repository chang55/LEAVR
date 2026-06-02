#include "config_parser.h"
#include "logger.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <algorithm>

namespace leavr {

std::string ConfigParser::Trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

std::string ConfigParser::MakeKey(const char* section, const char* key) const {
    std::string k = section;
    k += ".";
    k += key;
    return k;
}

int ConfigParser::Load(const char* file_path) {
    pthread_mutex_lock(&lock_);
    data_.clear();

    std::ifstream file(file_path);
    if (!file.is_open()) {
        LOG_WARN("Config: Cannot open %s", file_path);
        pthread_mutex_unlock(&lock_);
        return LEAVR_ERR_FILE_OPEN;
    }

    std::string current_section;
    std::string line;
    int line_num = 0;

    while (std::getline(file, line)) {
        line_num++;
        std::string trimmed = Trim(line);

        // 跳过空行和注释
        if (trimmed.empty() || trimmed[0] == '#' || trimmed[0] == ';') continue;

        // 解析段名 [Section]
        if (trimmed[0] == '[') {
            size_t end = trimmed.find(']');
            if (end != std::string::npos) {
                current_section = trimmed.substr(1, end - 1);
            }
            continue;
        }

        // 解析键值对 key = value
        size_t eq = trimmed.find('=');
        if (eq != std::string::npos) {
            std::string key = Trim(trimmed.substr(0, eq));
            std::string value = Trim(trimmed.substr(eq + 1));

            // 移除行内注释
            size_t comment = value.find('#');
            if (comment != std::string::npos) {
                value = Trim(value.substr(0, comment));
            }

            // 移除引号
            if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
                value = value.substr(1, value.size() - 2);
            }

            if (!current_section.empty() && !key.empty()) {
                data_[current_section][key] = value;
                LOG_DEBUG("Config: [%s] %s = %s", current_section.c_str(), key.c_str(), value.c_str());
            }
        }
    }

    LOG_INFO("Config: Loaded %zu sections from %s", data_.size(), file_path);
    pthread_mutex_unlock(&lock_);
    return LEAVR_OK;
}

int ConfigParser::Save(const char* file_path) {
    pthread_mutex_lock(&lock_);

    std::ofstream file(file_path);
    if (!file.is_open()) {
        pthread_mutex_unlock(&lock_);
        return LEAVR_ERR_FILE_OPEN;
    }

    file << "# LEAVR 执法记录仪配置文件\n";
    file << "# Auto-generated, do not edit manually while system is running\n\n";

    for (const auto& section : data_) {
        file << "[" << section.first << "]\n";
        for (const auto& kv : section.second) {
            file << kv.first << " = " << kv.second << "\n";
        }
        file << "\n";
    }

    file.close();
    LOG_INFO("Config: Saved to %s", file_path);
    pthread_mutex_unlock(&lock_);
    return LEAVR_OK;
}

const char* ConfigParser::GetString(const char* section, const char* key,
                                     const char* default_val) {
    pthread_mutex_lock(&lock_);
    auto sec_it = data_.find(section);
    if (sec_it != data_.end()) {
        auto kv_it = sec_it->second.find(key);
        if (kv_it != sec_it->second.end()) {
            // 注意: 返回的指针指向 map 中的 string::c_str(),
            // 在 unlock 前需要复制出来
            static thread_local std::string cached_val;
            cached_val = kv_it->second;
            pthread_mutex_unlock(&lock_);
            return cached_val.c_str();
        }
    }
    pthread_mutex_unlock(&lock_);
    return default_val;
}

int ConfigParser::GetInt(const char* section, const char* key, int default_val) {
    const char* val = GetString(section, key, "");
    if (val[0] == '\0') return default_val;
    return atoi(val);
}

bool ConfigParser::GetBool(const char* section, const char* key, bool default_val) {
    const char* val = GetString(section, key, "");
    if (val[0] == '\0') return default_val;
    std::string s(val);
    std::transform(s.begin(), s.end(), s.begin(), ::tolower);
    return (s == "1" || s == "true" || s == "yes" || s == "on" || s == "enable");
}

double ConfigParser::GetDouble(const char* section, const char* key, double default_val) {
    const char* val = GetString(section, key, "");
    if (val[0] == '\0') return default_val;
    return strtod(val, nullptr);
}

int ConfigParser::SetString(const char* section, const char* key, const char* value) {
    pthread_mutex_lock(&lock_);
    data_[section][key] = value;
    pthread_mutex_unlock(&lock_);
    return LEAVR_OK;
}

int ConfigParser::SetInt(const char* section, const char* key, int value) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%d", value);
    return SetString(section, key, buf);
}

int ConfigParser::SetBool(const char* section, const char* key, bool value) {
    return SetString(section, key, value ? "on" : "off");
}

} // namespace leavr