#include "argparser.h"
#include "common.h"
#include "config.h"
#include "tracer.h"
#include "util.h"

#include <csignal>
#include <cstdlib>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>

static void register_error_handler(Tracer& tracer) {
    static std::function<void(int)> global_signal_handler =
        [&, got_sigint = false](int sig) mutable {
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
        };

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

[[nodiscard]] static OsRelease parse_os() {
    OsRelease os;
    if (auto it = load_os_release(); it) {
        os = *it;
    } else {
        throw ArgumentParserException("Failed to load OS release information");
    }

    if (os.distribution != "ubuntu" && os.distribution != "debian") {
        throw std::runtime_error(
            STR("Unsupported distribution: " << os.distribution));
    }

    return os;
}

[[nodiscard]] static std::string base_image(OsRelease const& os_release) {
    auto r = os_release.release;
    if (os_release.distribution == "debian" && os_release.release.empty()) {
        r = "sid";
    }
    return STR(os_release.distribution << ':' << r);
}


static int run_from_options(Options& options) {
    
    Tracer tracer{options};

    // Interrupt signals generated in the terminal are delivered to the
    // active process group, which here includes both parent and child. A
    // signal manually generated and sent to an individual process (perhaps
    // with kill) will be delivered only to that process, regardless of
    // whether it is the parent or child. That is why we need to register a
    // signal handler that will terminate the tracee when the tracer
    // gets killed.

    register_error_handler(tracer);

    try {
        tracer.execute();
    } catch (TaskException& e) {
        std::cerr << e.what() << "\n";
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}


