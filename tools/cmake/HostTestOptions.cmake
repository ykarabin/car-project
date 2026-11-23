# Options for the host test builds (tests/ and firmware/remote-gateway/tests/).
#   -DHOST_SANITIZE=ON   AddressSanitizer + UndefinedBehaviorSanitizer, any finding fails the test
#   -DHOST_COVERAGE=ON   gcov coverage data for gcovr (CI uses GCC)
include_guard(GLOBAL)

option(HOST_SANITIZE "Build host tests with ASan and UBSan" OFF)
option(HOST_COVERAGE "Build host tests with coverage instrumentation" OFF)

if(HOST_SANITIZE)
    add_compile_options(-fsanitize=address,undefined -fno-sanitize-recover=all
                        -fno-omit-frame-pointer -g)
    add_link_options(-fsanitize=address,undefined)
endif()

if(HOST_COVERAGE)
    add_compile_options(--coverage -O0 -g)
    add_link_options(--coverage)
endif()
