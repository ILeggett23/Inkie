#pragma once
#include <filesystem>
#include <functional>
#include <span>
#include <string>
#include <vector>
namespace reader::storage {
// Off-contact only. A successful return means both content and directory entry
// reached fsync. A failure after rename is an uncertain commit, never "saved".
void replaceDurably(const std::filesystem::path&, std::span<const char>);
// Streaming producer is off-contact and writes only to the private temporary FD.
void replaceDurablyGenerated(const std::filesystem::path&,const std::function<void(int)>&);
// The same private-name, O_EXCL, rename-into-place write without fsync: for transport files that are verified on
// read and re-derived when missing or torn (a page helper's prepared.bin). Never for user data.
void replaceTransient(const std::filesystem::path&, std::span<const char>);
// An interrupted replaceDurably leaves its private ".inkbound-pending-<32 hex>"
// beside the target. Call these only when no writer for the directory can still
// be live: after waitpid proves the writing helper exited, or under a store lock
// with every writer drained. Removes only regular, private, single-link files
// owned by this user and not named in keep; anything else is left in place.
std::vector<std::string> interruptedWrites(int directory);
unsigned reclaimInterruptedWrites(int directory, std::span<const std::string> keep = {});
#ifdef INKBOUND_DURABILITY_TEST_FAULTS
enum class Fault { None, Write, FileSync, BeforeRename, DirectorySync };
void injectFault(Fault);
#endif
}
