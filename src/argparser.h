/**
 * @file argument_parser.hpp
 * @brief Modern C++23 command line argument parser with fluent interface
 */

#pragma once

#include <algorithm>
#include <functional>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

/**
 * @class ArgumentParser
 * @brief Command line argument parser with fluent builder interface
 *
 * Supports both short (-v) and long (--verbose) options, positional arguments,
 * required options, default values, and automatic help generation.
 */
class ArgumentParser {
  public:
    using Callback = std::function<void(std::string const&)>;

    /**
     * @brief Construct a new Argument Parser
     * @param description Program description for help message
     * @param program_name Program name for usage line (default: argv[0])
     */
    explicit ArgumentParser(std::string description,
                            std::string program_name = "")
        : description_(std::move(description)),
          program_name_(std::move(program_name)) {}

    /// @name Option Builder Interface
    /// @{

    /**
     * @brief Add an option with short name only
     * @param short_name Single character short name
     */
    ArgumentParser& add_option(char short_name) {
        return add_option_impl(std::string(1, short_name), "");
    }

    /**
     * @brief Add an option with long name only
     * @param long_name Long option name
     */
    ArgumentParser& add_option(std::string long_name) {
        return add_option_impl("", std::move(long_name));
    }

    /**
     * @brief Add an option with both short and long names
     * @param short_name Single character short name
     * @param long_name Long option name
     */
    ArgumentParser& add_option(char short_name, std::string long_name) {
        return add_option_impl(std::string(1, short_name),
                               std::move(long_name));
    }

    /// @}

    /// @name Positional Argument Interface
    /// @{

    /**
     * @brief Add a positional argument
     * @param name Argument name for help message
     * @param help Help description
     * @param required Whether argument is required
     */
    ArgumentParser& add_positional(std::string name, std::string help = "",
                                   bool required = true) {
        positionals_.push_back(
            {std::move(name), std::move(help), required, {}});
        return *this;
    }

    /// @}

    /// @name Configuration Methods
    /// @{

    /**
     * @brief Set help text for current option
     * @param help Help description text
     */
    ArgumentParser& help(std::string help) {
        current_option().help = std::move(help);
        return *this;
    }

    /**
     * @brief Set metavar for current option
     * @param metavar METAVAR string for help message
     */
    ArgumentParser& metavar(std::string metavar) {
        current_option().metavar = std::move(metavar);
        return *this;
    }

    /**
     * @brief Mark current option as requiring an argument
     */
    ArgumentParser& requires_argument() {
        current_option().requires_arg = true;
        return *this;
    }

    /**
     * @brief Set default value for current option
     * @param value Default value if option not provided
     */
    ArgumentParser& default_value(std::string value) {
        current_option().default_value = std::move(value);
        return *this;
    }

    /**
     * @brief Set callback for current option
     * @param callback Function to call when option is found
     */
    ArgumentParser& callback(Callback callback) {
        current_option().callback = std::move(callback);
        return *this;
    }

    /// @}

    /// @name Parsing and Querying
    /// @{

    /**
     * @brief Parse command line arguments
     * @param argc Argument count from main()
     * @param argv Argument values from main()
     * @throws std::runtime_error on parsing errors
     */
    void parse(int argc, char* argv[]) {
        args_ = {argv + 1, argv + argc};
        if (program_name_.empty())
            program_name_ = argv[0];

        parse_options();
        check_requirements();
        apply_defaults();
    }

    /**
     * @brief Check if option was provided
     * @param name Option name (short as string or long name)
     */
    [[nodiscard]] bool contains(std::string_view name) const {
        return find_option(name)->is_present;
    }

    /**
     * @brief Get value of option if provided
     * @param name Option name (short as string or long name)
     * @return std::optional<std::string> containing value if exists
     */
    [[nodiscard]] std::optional<std::string> get(std::string_view name) const {
        auto const& opt = *find_option(name);
        return opt.is_present ? opt.value : opt.default_value;
    }

    /// @}

    /**
     * @brief Generate help message
     * @return Formatted help string
     */
    [[nodiscard]] std::string help() const {
        std::ostringstream oss;
        oss << description_ << "\n\nUsage: " << program_name_;
        if (!options_.empty())
            oss << " [OPTIONS]";

        for (auto const& p : positionals_) {
            oss << ' ' << (p.required ? "<" : "[") << p.name
                << (p.required ? ">" : "...]");
        }

        if (!options_.empty()) {
            oss << "\n\nOptions:\n";
            for (auto const& opt : options_) {
                if (!opt.short_name.empty())
                    oss << '-' << opt.short_name
                        << (!opt.long_name.empty() ? ", " : "  ");
                if (!opt.long_name.empty())
                    oss << "--" << opt.long_name;

                if (opt.requires_arg)
                    oss << ' ' << (opt.metavar.empty() ? "ARG" : opt.metavar);

                oss << "\t" << opt.help;

                if (opt.default_value)
                    oss << " [default: " << *opt.default_value << "]";

                oss << '\n';
            }
        }

        oss << "\nPositional arguments:\n";
        for (auto const& p : positionals_) {
            oss << "  " << p.name << "\t" << p.help
                << (p.required ? "" : " [optional, accepts multiple]") << '\n';
        }
        return oss.str();
    }

  private:
    struct Option {
        std::string short_name;
        std::string long_name;
        std::string help;
        std::string metavar;
        bool requires_arg = false;
        bool is_present = false;
        std::optional<std::string> value;
        std::optional<std::string> default_value;
        Callback callback;
    };

    struct Positional {
        std::string name;
        std::string help;
        bool required;
        Callback callback;
    };

    ArgumentParser& add_option_impl(std::string short_name,
                                    std::string long_name) {
        options_.push_back({std::move(short_name), std::move(long_name)});
        return *this;
    }

    Option& current_option() {
        if (options_.empty())
            throw std::logic_error("No current option to configure");
        return options_.back();
    }

    Option const* find_option(std::string_view name) const {
        auto it = std::find_if(
            options_.begin(), options_.end(), [&](Option const& o) {
                return o.short_name == name || o.long_name == name;
            });

        if (it == options_.end())
            throw std::invalid_argument("Unknown option: " + std::string(name));

        return &*it;
    }

    void parse_options() {
        for (size_t i = 0; i < args_.size(); ++i) {
            if (args_[i][0] == '-') {
                args_[i][1] == '-' ? parse_long(i) : parse_short(i);
            } else {
                parse_positional(i);
            }
        }
    }

    void parse_long(size_t& i) {
        auto [name, value] = split_arg(args_[i].substr(2));
        handle_option(name, value, i);
    }

    void parse_short(size_t& i) {
        auto const& arg = args_[i].substr(1);
        for (size_t j = 0; j < arg.size(); ++j) {
            auto opt = find_option(std::string(1, arg[j]));
            if (opt->requires_arg) {
                handle_option(opt->short_name, arg.substr(j + 1), i);
                break; // Argument consumes remaining characters
            } else {
                handle_option(opt->short_name, "", i);
            }
        }
    }

    void parse_positional(size_t& i) {
        if (pos_index_ < positionals_.size()) {
            positionals_[pos_index_++].callback(args_[i]);
        } else if (!positionals_.empty() && !positionals_.back().required) {
            positionals_.back().callback(args_[i]);
        } else {
            throw std::runtime_error("Unexpected positional argument: " +
                                     args_[i]);
        }
        ++i;
    }

    void handle_option(std::string_view name, std::string_view value,
                       size_t& i) {
        auto& opt = *find_option(name);
        opt.is_present = true;

        if (opt.requires_arg) {
            if (value.empty() && ++i < args_.size())
                value = args_[i];

            if (value.empty())
                throw std::runtime_error("Option requires argument: " +
                                         std::string(name));

            opt.value = std::string(value);
        }

        if (opt.callback)
            opt.callback(opt.value ? *opt.value : "");
    }

    static auto split_arg(std::string_view arg) {
        auto const eq = arg.find('=');
        return std::pair{arg.substr(0, eq),
                         eq == arg.npos ? "" : arg.substr(eq + 1)};
    }

    void check_requirements() const {
        for (auto const& opt : options_) {
            if (opt.is_present && opt.requires_arg && !opt.value)
                throw std::runtime_error("Option requires argument: " +
                                         opt_name(opt));

            if (opt.requires_arg && !opt.default_value && !opt.is_present)
                throw std::runtime_error("Missing required option: " +
                                         opt_name(opt));
        }

        if (pos_index_ < positionals_.size()) {
            for (size_t i = pos_index_; i < positionals_.size(); ++i) {
                if (positionals_[i].required)
                    throw std::runtime_error("Missing required positional: " +
                                             positionals_[i].name);
            }
        }
    }

    void apply_defaults() {
        for (auto& opt : options_) {
            if (!opt.is_present && opt.default_value) {
                opt.value = opt.default_value;
                if (opt.callback)
                    opt.callback(*opt.value);
            }
        }
    }

    static std::string opt_name(Option const& opt) {
        return !opt.short_name.empty() ? "-" + opt.short_name
                                       : "--" + opt.long_name;
    }

    std::string description_;
    std::string program_name_;
    std::vector<Option> options_;
    std::vector<Positional> positionals_;
    std::vector<std::string> args_;
    size_t pos_index_ = 0;
};
