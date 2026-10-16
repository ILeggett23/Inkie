#pragma once
#include <filesystem>
namespace library {
// Helper-only policy gate. Throws on unsafe content, parse failures or limits.
// The caller keeps this private staged source immutable through inspection.
void validatePdfPolicy(const std::filesystem::path&);
}
