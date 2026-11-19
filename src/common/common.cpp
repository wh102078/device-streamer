#include "common/common.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>

extern "C" {
#include <libavutil/error.h>
}

namespace ds {

void die(const char* what) {
    std::fprintf(stderr, "%s failed: %s\n", what, std::strerror(errno));
    std::exit(EXIT_FAILURE);
}

void ff_die(const char* what, int err) {
    char buf[AV_ERROR_MAX_STRING_SIZE]{};
    av_strerror(err, buf, sizeof(buf));
    std::fprintf(stderr, "%s failed: %s\n", what, buf);
    std::exit(EXIT_FAILURE);
}

}  // namespace ds
