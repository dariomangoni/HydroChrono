#include <hydroc/helper.h>


#include <filesystem>  // C++17
#include <cstdlib>
#include <iostream>
#include <vector>
#include <memory>

using std::filesystem::path;

static path DATADIR{};



int hydroc::setInitialEnvironment(int argc, char* argv[]) noexcept {
    const char* env_p = std::getenv("HYDRO_CHRONO_DATA_DIR");

    if (env_p == nullptr) {
        if (argc < 2) {
            std::cerr << "Usage: .exe [<datadir>] or set HYDRO_CHRONO_DATA_DIR environement variable" << std::endl;
            return 1;
        } else {
            DATADIR = absolute(path(argv[1]));
        }
    } else {
        DATADIR = absolute(path(env_p));
    }
    return 0;
}


std::string hydroc::getDataDir() noexcept {
    return DATADIR.lexically_normal().generic_string();
}