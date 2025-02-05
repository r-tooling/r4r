#ifndef ARGPARSER_H
#define ARGPARSER_H

#include <algorithm>
#include <functional>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

class ArgumentParserException : public std::runtime_error {
  public:
    ArgumentParserException(std::string const& message)
        : std::runtime_error(message) {}
};

class ArgumentParser {
  public:
    using Callback = std::function<void(std::string const&)>;

    struct Option {
        std::string short_name;
        std::string long_name;
        std::string help;
        std::optional<std::string> has_arg;
        bool is_required{false};
        std::optional<std::string> default_value;
        Callback callback;
        std::optional<std::string> value;

        Option(std::string sn, std::string ln)
            : short_name(std::move(sn)), long_name(std::move(ln)) {}

        Option& with_help(std::string text) {
            help = std::move(text);
            return *this;
        }
        Option& with_argument(std::string metavar = "ARG") {
            has_arg = metavar;
            return *this;
        }
        Option& required() {
            is_required = true;
            return *this;
        }
        Option& with_default(std::string val) {
            default_value = std::move(val);
            return *this;
        }
        Option& with_callback(Callback cb) {
            callback = std::move(cb);
            return *this;
        }
    };

    struct Positional {
        std::string name;
        std::string help;
        bool is_required = false;
        bool allows_multiple = false;
        Callback callback;
        std::vector<std::string> values;

        explicit Positional(std::string name) : name(std::move(name)) {}

        Positional& with_help(std::string text) {
            help = std::move(text);
            return *this;
        }
        Positional& required() {
            is_required = true;
            return *this;
        }
        Positional& multiple() {
            allows_multiple = true;
            return *this;
        }
        Positional& with_callback(Callback cb) {
            callback = std::move(cb);
            return *this;
        }
    };

    class ParseResult {
        std::vector<Option> const& options_;
        std::vector<Positional> const& positionals_;

      public:
        ParseResult(std::vector<Option> const& opts,
                    std::vector<Positional> const& pos)
            : options_(opts), positionals_(pos) {}

        bool contains(std::string_view name) const {
            return std::any_of(
                options_.begin(), options_.end(), [&](Option const& opt) {
                    return (opt.short_name == name || opt.long_name == name) &&
                           opt.value.has_value();
                });
        }

        std::optional<std::string> get(std::string_view name) const {
            auto const it = std::find_if(
                options_.begin(), options_.end(), [&](Option const& opt) {
                    return opt.short_name == name || opt.long_name == name;
                });
            return it != options_.end() ? it->value
                                        : std::optional<std::string>{};
        }

        std::vector<std::string> get_positional(std::string_view name) const {
            auto const it = std::find_if(
                positionals_.begin(), positionals_.end(),
                [&](Positional const& p) { return p.name == name; });
            return it != positionals_.end() ? it->values
                                            : std::vector<std::string>{};
        }
    };

    ArgumentParser(std::string program_name, std::string description = "")
        : program_name_(std::move(program_name)),
          description_(std::move(description)) {}

    Option& add_option(char short_name) {
        return options_.emplace_back(std::string(1, short_name), "");
    }

    Option& add_option(std::string const& long_name) {
        return options_.emplace_back("", long_name);
    }

    Option& add_option(char short_name, std::string const& long_name) {
        return options_.emplace_back(std::string(1, short_name), long_name);
    }

    Positional& add_positional(std::string name) {
        return positionals_.emplace_back(std::move(name));
    }

    ParseResult parse(int argc, char* argv[]) {
        args_ = {argv + 1, argv + argc};
        if (program_name_.empty() && argc > 0) {
            program_name_ = argv[0];
        }

        size_t current_arg = 0;
        size_t positional_index = 0;

        while (current_arg < args_.size()) {
            auto const& arg = args_[current_arg];

            if (arg.starts_with("--")) {
                parse_long_option(arg, current_arg);
            } else if (arg.starts_with('-')) {
                parse_short_options(arg, current_arg);
            } else {
                parse_positional(arg, positional_index);
            }
            current_arg++;
        }

        validate_requirements();
        apply_defaults();
        return {options_, positionals_};
    }

    [[nodiscard]] std::string help() const {
        std::ostringstream oss;

        if (!description_.empty()) {
            oss << description_ << "\n\n";
        }

        oss << "Usage: " << program_name_;
        if (!options_.empty()) {
            oss << " [OPTIONS]";
        }

        for (auto const& pos : positionals_) {
            oss << ' ' << (pos.is_required ? "<" : "[") << pos.name
                << (pos.is_required ? ">" : "...]");
        }

        size_t opt_line_max_length = 0;
        for (auto const& opt : options_) {
            size_t opt_line_length = 0;
            if (!opt.short_name.empty()) {
                opt_line_length += 4; // '-x, '
            }
            if (!opt.long_name.empty()) {
                opt_line_length += 2 + opt.long_name.size(); // '--xx'
            }
            if (opt.has_arg) {
                opt_line_length += 1 + opt.has_arg->size(); // ' XX'
            }
            opt_line_max_length =
                std::max(opt_line_max_length, opt_line_length);
        }

        if (!options_.empty()) {
            oss << "\n\nOptions:\n";
            for (auto const& opt : options_) {
                oss << "  ";
                size_t pos = oss.tellp();

                if (!opt.short_name.empty()) {
                    oss << '-' << opt.short_name;
                    if (!opt.long_name.empty()) {
                        oss << ", ";
                    }
                }
                if (!opt.long_name.empty()) {
                    oss << "--" << opt.long_name;
                }

                if (opt.has_arg) {
                    oss << ' ' << *opt.has_arg;
                }

                if (!opt.help.empty() || opt.default_value.has_value()) {
                    size_t fill = 4 + opt_line_max_length -
                                  (static_cast<size_t>(oss.tellp()) - pos);
                    oss << std::string(fill, ' ') << opt.help;
                }

                if (opt.default_value) {
                    oss << " [default: " << *opt.default_value << "]";
                }

                oss << '\n';
            }
        }

        opt_line_max_length = 0;
        for (auto const& opt : positionals_) {
            opt_line_max_length =
                std::max(opt_line_max_length, opt.name.size());
        }

        if (!positionals_.empty()) {
            oss << "\nPositional arguments:\n";
            for (auto const& pos : positionals_) {
                oss << "  " << pos.name
                    << std::string(4 + opt_line_max_length - pos.name.size(),
                                   ' ')
                    << pos.help << '\n';
            }
        }

        return oss.str();
    }

  private:
    std::string program_name_;
    std::string description_;
    std::vector<Option> options_;
    std::vector<Positional> positionals_;
    std::vector<std::string> args_;

    void parse_short_options(std::string const& arg, size_t& current_arg) {
        auto const options = arg.substr(1);
        for (size_t i = 0; i < options.size(); ++i) {
            char const c = options[i];
            auto& opt = find_option(std::string(1, c));

            if (opt.has_arg) {
                std::string value;
                if (i + 1 < options.size()) {
                    value = options.substr(i + 1);
                    i = options.size();
                } else if (current_arg + 1 < args_.size()) {
                    value = args_[++current_arg];
                }
                if (value.empty()) {
                    throw ArgumentParserException(
                        "Option requires argument: -" + opt.short_name);
                }
                opt.value = value;
                if (opt.callback) {
                    opt.callback(*opt.value);
                }
            } else {
                opt.value = "";
                if (opt.callback) {
                    opt.callback("");
                }
            }
        }
    }

    void parse_long_option(std::string const& arg, size_t& current_arg) {
        auto const equal_pos = arg.find('=', 2);
        std::string const name = arg.substr(2, equal_pos - 2);
        auto& opt = find_option(name);

        if (opt.has_arg) {
            std::string value;
            if (equal_pos != std::string::npos) {
                value = arg.substr(equal_pos + 1);
            } else if (++current_arg < args_.size()) {
                value = args_[current_arg];
            }
            if (value.empty()) {
                throw ArgumentParserException("Option requires argument: --" +
                                              opt.long_name);
            }
            opt.value = value;
            if (opt.callback) {
                opt.callback(*opt.value);
            }
        } else {
            opt.value = "";
            if (opt.callback) {
                opt.callback("");
            }
        }
    }

    void parse_positional(std::string const& arg, size_t& positional_index) {
        if (positional_index < positionals_.size()) {
            auto& pos = positionals_[positional_index];
            pos.values.push_back(arg);
            if (pos.callback) {
                pos.callback(arg);
            }
            if (!pos.allows_multiple) {
                positional_index++;
            }
        } else {
            throw ArgumentParserException("Unexpected positional argument: " +
                                          arg);
        }
    }

    Option& find_option(std::string_view name) {
        auto it = std::find_if(
            options_.begin(), options_.end(), [&](Option const& opt) {
                return opt.short_name == name || opt.long_name == name;
            });
        if (it == options_.end()) {
            throw ArgumentParserException("Unknown option: " +
                                          std::string(name));
        }
        return *it;
    }

    void validate_requirements() const {
        for (auto const& opt : options_) {
            if (opt.is_required && !opt.value.has_value()) {
                throw ArgumentParserException("Missing required option: " +
                                              format_option_name(opt));
            }
        }
        for (auto const& pos : positionals_) {
            if (pos.is_required && pos.values.empty()) {
                throw ArgumentParserException("Missing required positional: " +
                                              pos.name);
            }
        }
    }

    void apply_defaults() {
        for (auto& opt : options_) {
            if (!opt.value.has_value() && opt.default_value) {
                opt.value = opt.default_value;
                if (opt.callback) {
                    opt.callback(*opt.value);
                }
            }
        }
    }

    static std::string format_option_name(Option const& opt) {
        std::string name;
        if (!opt.short_name.empty()) {
            name += "-" + opt.short_name;
        }
        if (!opt.long_name.empty()) {
            name += (name.empty() ? "--" : "/--") + opt.long_name;
        }
        return name;
    }
};

#endif // ARGPARSER_H
