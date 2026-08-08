# signalsmith-stretch: MIT, header-only, pitch-preserving time-stretch.
# Tag 1.1.0 is self-contained (bundles its dsp/ helpers), so no submodules are
# needed. Exposes an INTERFACE target `pablo_signalsmith` carrying the include
# path; consumers include "signalsmith-stretch.h".
include(FetchContent)

FetchContent_Declare(signalsmith_stretch
    GIT_REPOSITORY https://github.com/Signalsmith-Audio/signalsmith-stretch.git
    GIT_TAG 1.1.0
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(signalsmith_stretch)

add_library(pablo_signalsmith INTERFACE)
target_include_directories(pablo_signalsmith INTERFACE ${signalsmith_stretch_SOURCE_DIR})
