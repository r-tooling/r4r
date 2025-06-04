#include "../include/r4r/r4r_lib.h"
#include "main_common.h"

static bool isEmptyOrWhitespace(const std::string& str) {
    return str.empty() || std::all_of(str.begin(), str.end(), [](unsigned char c) {
        return std::isspace(c);
    });
}

int r4r_trace_expression(std::string expression, std::string output, std::string containerName, std::string baseImage) {
    Options options;
    options.os_release = parse_os();

    options.docker_base_image = baseImage;
    if (isEmptyOrWhitespace(options.docker_base_image))
        options.docker_base_image = base_image(options.os_release);
 
    // TODO replace
   std::string cacheFolder = "/home/sebastiankrynski/r4rcache";
  
    
    //-vvv (verbose)
    --options.log_level;
    --options.log_level;
    --options.log_level;

    options.output_dir = output;
    options.skip_manifest = true; 
    options.default_image_file = cacheFolder + "/" + options.docker_base_image + ".cache";
    options.docker_image_tag = "r4r/" + containerName;
    options.docker_container_name = "r4r-container-" + containerName;

   
    options.cmd.push_back("R");
    options.cmd.push_back("-e");
    options.cmd.push_back(expression);

    std::cerr << options.docker_base_image  << "\n";
    
    if (1==1)
        return 1;
 
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