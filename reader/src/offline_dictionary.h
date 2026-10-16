#pragma once
#include "owned/DictionaryProvider.h"
#include <filesystem>
#include <memory>
namespace inkbound::dictionary {
// Semantic/helper thread only. Bounded on-demand reads; no network or writes.
std::unique_ptr<DictionaryProvider> offlineDictionary(const std::filesystem::path&);
std::unique_ptr<DictionaryProvider> offlineDictionaryChain(const std::filesystem::path& assetDirectory,std::string_view context={});
}
