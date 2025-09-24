#include "experience_io.h"

#include <cstdio>
#include <string>

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: %s path.exp\n", argv[0]);
        return 1;
    }

    const std::string path = argv[1];
    bool ok                 = Experience_OpenForReadWrite(path);
    std::printf("%s\n", ok ? "OK" : "FAIL");
    return ok ? 0 : 2;
}

