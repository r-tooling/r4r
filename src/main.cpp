#include "argparser.h"
#include "common.h"
#include "config.h"
#include "tracer.h"

#include <csignal>
#include <cstdlib>
#include <iostream>
#include <string>

std::function<void(int)> global_signal_handler;

void register_signal_handlers(std::function<void(int)> handler) {
    global_signal_handler = std::move(handler);
    std::array signals = {SIGINT, SIGTERM};
    for (int sig : signals) {
        auto status = signal(sig, [](int sig) { global_signal_handler(sig); });
        if (status == SIG_ERR) {
            throw make_system_error(errno,
                                    STR("Failed to register signal "
                                        << strsignal(sig) << " handler"));
        }
    }
}

Options parse_cmd_args(int argc, char* argv[]) {
    Options opts;
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
    parser.add_option("result")
        .with_help("Path to a result file")
        .with_argument("PATH")
        .with_callback([&](auto& arg) { opts.results.insert(arg); });
    parser.add_option("output")
        .with_help("Path for the output")
        .with_argument("PATH")
        .with_callback([&](auto& arg) { opts.output_dir = arg; });
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

    try {
        parser.parse(argc, argv);
        return opts;
    } catch (ArgumentParserException const& e) {
        std::cerr << kBinaryName << ": " << e.what() << '\n';
        std::cerr << kBinaryName << ": "
                  << "try '" << kBinaryName << " --help' for more information"
                  << '\n';

        exit(1);
    }
}

int main(int argc, char* argv[]) {
    Options options = parse_cmd_args(argc, argv);
    Tracer tracer{options};

    // Interrupt signals generated in the terminal are delivered to the
    // active process group, which here includes both parent and child. A
    // signal manually generated and sent to an individual process (perhaps
    // with kill) will be delivered only to that process, regardless of
    // whether it is the parent or child. That is why we need to register a
    // signal handler that will terminate the tracee when the tracer
    // gets killed.

    register_signal_handlers([&, got_sigint = false](int sig) mutable {
        switch (sig) {
        case SIGTERM:
            tracer.stop();
            exit(1);
        case SIGINT:
            if (got_sigint) {
                std::cerr << "SIGINT twice, exiting the tracer!";
                exit(1);
            } else {
                std::cerr << "SIGINT, stopping the current task...";
                tracer.stop();
                got_sigint = true;
            }
            break;
        default:
            UNREACHABLE();
        }
    });

    try {
        tracer.execute();
        return 0;
    } catch (TaskException& e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
}
