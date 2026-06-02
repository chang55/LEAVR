/**
 * @file config_parser.h
 * @brief INI 配置文件解析器
 */

#ifndef LEAVR_UTILS_CONFIG_PARSER_H
#define LEAVR_UTILS_CONFIG_PARSER_H

#include "leavr_interfaces.h"
#include <map>
#include <string>
#include <pthread.h>

namespace leavr {

class ConfigParser : public IConfigParser {
public:
    ConfigParser() = default;
    ~ConfigParser() override = default;

    int Load(const char* file_path) override;
    int Save(const char* file_path) override;

    const char* GetString(const char* section, const char* key,
                           const char* default_val) override;
    int GetInt(const char* section, const char* key, int default_val) override;
    bool GetBool(const char* section, const char* key, bool default_val) override;
    double GetDouble(const char* section, const char* key, double default_val) override;

    int SetString(const char* section, const char* key, const char* value) override;
    int SetInt(const char* section, const char* key, int value) override;
    int SetBool(const char* section, const char* key, bool value) override;

private:
    using SectionMap = std::map<std::string, std::string>;
    std::map<std::string, SectionMap> data_;
    pthread_mutex_t lock_ = PTHREAD_MUTEX_INITIALIZER;

    std::string MakeKey(const char* section, const char* key) const;
    static std::string Trim(const std::string& s);
};

} // namespace leavr

#endif // LEAVR_UTILS_CONFIG_PARSER_H