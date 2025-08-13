#include "main_common.h"

static void parse_cmd_args(Options& opts, std::span<char const*> args) {
    opts.docker_base_image = base_image(opts.os_release);

    ArgumentParser parser{std::string(kBinaryName)};

    parser.add_option('v', "verbose")
        .with_help("Make the tool more talkative (allow multiple)")
        .with_callback([&](auto&) { --opts.log_level; });
    parser.add_option("docker-image-tag")
        .with_help("The docker image tag")
        .with_default(opts.docker_image_tag)
        .with_argument("NAME")
        .with_callback([&](auto& arg) { opts.docker_image_tag = arg; });
    parser.add_option("docker-container-name")
        .with_help("The docker container name")
        .with_default(opts.docker_container_name)
        .with_argument("NAME")
        .with_callback([&](auto& arg) { opts.docker_container_name = arg; });
    parser.add_option("docker-base-image")
        .with_help("The docker base image")
        .with_default(opts.docker_base_image)
        .with_argument("NAME")
        .with_callback([&](auto& arg) { opts.docker_base_image = arg; });
    parser.add_option("result")
        .with_help("Path to a result file")
        .with_argument("PATH")
        .with_callback([&](auto& arg) { opts.results.insert(arg); });
    parser.add_option("output")
        .with_help("Path for the output")
        .with_argument("PATH")
        .with_callback([&](auto& arg) { opts.output_dir = arg; });
    parser.add_option("skip-make")
        .with_help("Do not run the generated makefile")
        .with_callback([&](auto&) { opts.run_make = false; });
    parser.add_option("skip-manifest")
        .with_help("Do not generate the manifest")
        .with_callback([&](auto&) { opts.skip_manifest = true; });
    parser.add_option("default-image-file")
        .with_help("Path to the default image file")
        .with_argument("PATH")
        .with_callback([&](auto& arg) { opts.default_image_file = arg; });

    parser.add_option("help")
        .with_help("Print this message")
        .with_callback([&](auto&) {
            std::cout << parser.help();
            exit(0);
        });

    parser.add_positional("command")
        .required()
        .multiple()
        .with_help("The program to trace")
        .with_callback([&](auto& arg) { opts.cmd.push_back(arg); });

    parser.parse(args);
}

static int do_main(std::span<char const*> args) {
    Options options;

    try {
        options.os_release = parse_os();
        parse_cmd_args(options, args);
    } catch (ArgumentParserException const& e) {
        std::cerr << kBinaryName << ": " << e.what() << '\n';
        std::cerr << kBinaryName << ": "
                  << "try '" << kBinaryName << " --help' for more information"
                  << '\n';

        exit(1);
    }

    return run_from_options(options);
}

int main(int argc, char* argv[]) {
    try {
        std::span<char const*> args(const_cast<char const**>(argv), argc);

        return do_main(args);
    } catch (std::exception const& e) {
        std::cerr << "Unhandled exception: " << e.what() << '\n';
        return EXIT_FAILURE;
    } catch (...) {
        std::cerr << "Unhandled unknown exception." << '\n';
        return EXIT_FAILURE;
    }
}
