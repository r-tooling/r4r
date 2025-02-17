#ifndef MANIFEST_FORMAT_H
#define MANIFEST_FORMAT_H

#include "common.h"
#include "util.h"
#include "util_io.h"
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

class ManifestFormat {
public:
    static constexpr char comment() noexcept { return '#'; }
    static constexpr char const* prefixed_comment() noexcept { return "# "; }

    struct Section {
        std::string name;
        std::string content;
    };

    ManifestFormat() = default;

    using Iterator = std::vector<Section>::iterator;
    using ConstIterator = std::vector<Section>::const_iterator;

    [[nodiscard]] Iterator begin() { return sections_.begin(); }
    [[nodiscard]] Iterator end() { return sections_.end(); }
    [[nodiscard]] ConstIterator begin() const { return sections_.begin(); }
    [[nodiscard]] ConstIterator end() const { return sections_.end(); }
    [[nodiscard]] ConstIterator cbegin() const { return sections_.cbegin(); }
    [[nodiscard]] ConstIterator cend() const { return sections_.cend(); }

    void set_preamble(std::string preamble) { preamble_ = std::move(preamble); }

    Section* get_section(std::string const& name);
    Section& add_section(Section const& section);

    void write(std::ostream& out) const;

private:
    std::string preamble_;
    std::vector<Section> sections_;

    friend std::ostream& operator<<(std::ostream& os,
                                    ManifestFormat const& format) {
        format.write(os);
        return os;
    }

    friend std::istream& operator>>(std::istream& in, ManifestFormat& format) {
        std::string line;
        Section* section = nullptr;

        while (std::getline(in, line)) {
            if (auto pos = line.find(comment()); pos != std::string::npos) {
                line = line.substr(0, pos);
            }

            line = string_trim(line);
            if (line.empty()) {
                continue;
            }
            if (is_section_header(line)) {
                std::string name = line.substr(0, line.size() - 1);
                section = &format.add_section({.name = name, .content = ""});
                continue;
            }
            if (section == nullptr) {
                throw std::runtime_error(
                    "Content line encountered before any section header: " +
                    line);
            }
            if (!section->content.empty()) {
                section->content.push_back('\n');
            }

            if (line.starts_with(comment())) {
                continue;
            }

            auto pos = line.find(comment());
            if (pos != std::string::npos) {
                line = line.substr(0, pos);
                line = string_trim(line);
            }

            section->content.append(line);
        }

        return in;
    }

    static bool is_valid_section_name(std::string_view name);

    static bool is_section_header(std::string_view line);
};

inline ManifestFormat::Section* ManifestFormat::get_section(
    std::string const& name) {
    auto it =
        std::find_if(sections_.begin(), sections_.end(),
                     [&](Section const& x) { return x.name == name; });
    if (it == sections_.end()) {
        return nullptr;
    }
    return &*it;
}

inline ManifestFormat::Section& ManifestFormat::add_section(
    Section const& section) {
    if (!is_valid_section_name(section.name)) {
        throw std::invalid_argument("Invalid section name: " +
                                    section.name);
    }

    if (get_section(section.name) != nullptr) {
        throw std::runtime_error(
            STR("section: " << section.name << " already exists"));
    }

    return sections_.emplace_back(section);
}

inline void ManifestFormat::write(std::ostream& out) const {
    if (!preamble_.empty()) {
        with_prefixed_ostream(out, prefixed_comment(),
                              [&] { out << preamble_; });
        out << "\n\n";
    }

    for (auto const& [name, content] : sections_) {
        out << name << ':' << '\n';
        with_prefixed_ostream(out, "  ", [&] { out << content; });
        out << "\n\n";
    }
}

inline bool ManifestFormat::is_valid_section_name(std::string_view name) {
    if (name.empty()) {
        return false;
    }

    if ((std::isalpha(name[0]) == 0) && name[0] != '_') {
        return false;
    }

    for (char ch : name) {
        if ((std::isalnum(ch) == 0) && ch != '_') {
            return false;
        }
    }

    return true;
}

inline bool ManifestFormat::is_section_header(std::string_view line) {
    if (line.empty() || line.back() != ':') {
        return false;
    }
    auto name = line.substr(0, line.size() - 1);
    return is_valid_section_name(name);
}

#endif // MANIFEST_FORMAT_H
