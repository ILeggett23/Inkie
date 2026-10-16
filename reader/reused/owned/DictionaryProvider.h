#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace inkbound::dictionary {

struct DictionarySense {
  std::string partOfSpeech;
  std::string definition;
  std::string example;
};

struct DictionaryEntry {
  std::string language;
  std::string headword;
  std::string pronunciation;
  std::vector<DictionarySense> senses;
  std::string sourceId;
  std::string sourceAttribution;
  std::string datasetVersion;
};

class DictionaryProvider {
 public:
  virtual ~DictionaryProvider() = default;
  [[nodiscard]] virtual std::string sourceIdentifier() const = 0;
  [[nodiscard]] virtual std::string sourceAttribution() const = 0;
  [[nodiscard]] virtual bool isAvailable() const = 0;
  [[nodiscard]] virtual bool isOffline() const = 0;
  [[nodiscard]] virtual std::vector<std::string> supportedLanguages() const = 0;
  virtual std::optional<DictionaryEntry> lookup(std::string_view language,
                                                std::string_view word) = 0;
};

}  // namespace inkbound::dictionary

