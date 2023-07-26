#pragma once

#include <fstream>
#include <optional>
#include <string>
#include <unordered_map>

class Dictionary {
public:
  [[nodiscard]] static Dictionary& instance() {
    static Dictionary singleton;
    return singleton;
  }

  void LoadCsv(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) exit(2);
    for (std::string line; std::getline(file, line);) {
      auto pair = Split(line);
      this->Add(pair);
    }
    file.close();
  }

  std::optional<std::string> Get(const std::string& key) {
    if (key.empty()) {
      return std::nullopt;
    }
    if (auto it = this->dict.find(key); it != this->dict.end()) {
      if (auto value = it->second; !value.empty()) {
        return value;
      }
    }
    return std::nullopt;
  }

  void Add(std::pair<std::string, std::string>& pair) {
    this->dict.emplace(pair);
  }

  size_t Size() const {
    return this->dict.size();
  }

private:
  Dictionary() {}
  Dictionary(const Dictionary&) = delete;
  Dictionary(Dictionary&&) = delete;

  ~Dictionary() {
    this->dict.clear();
  };

  void ReplaceAll(std::string& subject, const std::string& search, const std::string& replace) {
    size_t pos = 0;
    while ((pos = subject.find(search, pos)) != std::string::npos) {
      subject.replace(pos, search.size(), replace);
      pos += replace.size();
    }
  }

  std::pair<std::string, std::string> Split(const std::string& str) {
    std::string delimiter = "\",\"";
    auto delimiter_pos = str.find(delimiter);
    auto key = str.substr(1, delimiter_pos - 1);
    auto value = str.substr(delimiter_pos + 3, str.size() - key.size() - 5);
    return std::make_pair(Sanitize(key), Sanitize(value));
  }

  std::string Sanitize(std::string& str) {
    ReplaceAll(str, R"("")", "\"");
    return str;
  }

  std::unordered_map<std::string, std::string> dict;
};
