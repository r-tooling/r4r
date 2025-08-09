#include "../include/r4r/r4r_lib.h"
#include "main_common.h"

static bool isEmptyOrWhitespace(std::string const& str) {
    return str.empty() ||
           std::all_of(str.begin(), str.end(),
                       [](unsigned char c) { return std::isspace(c); });
}

int r4r_trace_expression(std::string expression, std::string output,
                         std::string imageTag, std::string containerName,
                         std::string baseImage, bool skipManifest) {
    Options options;
    options.os_release = parse_os();
    options.docker_base_image = base_image(options.os_release);

    if (!isEmptyOrWhitespace(baseImage)) {
        options.docker_base_image = baseImage;
    }

    //-vvv (verbose)
    --options.log_level;
    --options.log_level;
    //--options.log_level;

    options.output_dir = output;
    options.skip_manifest = skipManifest;

    options.default_image_file = get_user_cache_dir() / kBinaryName /
                                 (options.docker_base_image + ".cache");

    options.docker_image_tag = imageTag;
    options.docker_container_name = containerName;

    options.cmd.push_back("R");
    options.cmd.push_back("-e");
    options.cmd.push_back(expression);

    try {
        return run_from_options(options);
    } catch (std::exception const& e) {
        std::cerr << "Unhandled exception: " << e.what() << '\n';
        return EXIT_FAILURE;
    } catch (...) {
        std::cerr << "Unhandled unknown exception." << '\n';
        return EXIT_FAILURE;
    }
}