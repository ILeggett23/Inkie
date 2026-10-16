#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

namespace inkbound::document {

struct PageRect final {
    double x{0.0};
    double y{0.0};
    double width{0.0};
    double height{0.0};

    friend bool operator==(const PageRect&, const PageRect&) = default;
};

struct NormalizedRect final {
    double x{0.0};
    double y{0.0};
    double width{0.0};
    double height{0.0};

    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] PageRect toPage(double pageWidth, double pageHeight) const;
    [[nodiscard]] static NormalizedRect fromPage(
        const PageRect& rect,
        double pageWidth,
        double pageHeight);

    friend bool operator==(const NormalizedRect&, const NormalizedRect&) = default;
};

struct TextRange final {
    std::uint64_t start{0};
    std::uint64_t length{0};

    friend bool operator==(const TextRange&, const TextRange&) = default;
};

struct PdfAnchor final {
    std::uint32_t pageIndex{0};
    std::optional<NormalizedRect> normalizedRect;
    std::optional<TextRange> textRange;
    std::string exactQuote;
    std::string quotePrefix;
    std::string quoteSuffix;
    std::string pageContentHash;

    friend bool operator==(const PdfAnchor&, const PdfAnchor&) = default;
};

struct EpubAnchor final {
    std::string spineItem;
    std::string structuralPath;
    double normalizedCharacterOffset{0.0};
    std::string exactQuote;
    std::string quotePrefix;
    std::string quoteSuffix;
    std::string chapterContentHash;
    std::uint32_t reflowConfigurationVersion{1};

    friend bool operator==(const EpubAnchor&, const EpubAnchor&) = default;
};

enum class AnchorKind {
    pdf,
    epub,
};

class DocumentAnchor final {
public:
    static constexpr std::uint32_t serializationVersion = 1;

    [[nodiscard]] static DocumentAnchor forPdf(
        std::string documentId,
        std::string documentContentHash,
        PdfAnchor anchor);
    [[nodiscard]] static DocumentAnchor forEpub(
        std::string documentId,
        std::string documentContentHash,
        EpubAnchor anchor);

    [[nodiscard]] AnchorKind kind() const noexcept;
    [[nodiscard]] const std::string& documentId() const noexcept;
    [[nodiscard]] const std::string& documentContentHash() const noexcept;
    [[nodiscard]] const PdfAnchor* pdf() const noexcept;
    [[nodiscard]] const EpubAnchor* epub() const noexcept;

    [[nodiscard]] bool validate(std::string* error = nullptr) const noexcept;
    // Deterministic versioned JSON. Key order and floating-point formatting are
    // stable, making serialized anchors suitable for hashing and test fixtures.
    [[nodiscard]] std::string serialize() const;
    [[nodiscard]] static std::optional<DocumentAnchor> deserialize(
        std::string_view json,
        std::string* error = nullptr) noexcept;

    friend bool operator==(const DocumentAnchor&, const DocumentAnchor&) = default;

private:
    DocumentAnchor(
        std::string documentId,
        std::string documentContentHash,
        std::variant<PdfAnchor, EpubAnchor> value);

    std::string documentId_;
    std::string documentContentHash_;
    std::variant<PdfAnchor, EpubAnchor> value_;
};

} // namespace inkbound::document
