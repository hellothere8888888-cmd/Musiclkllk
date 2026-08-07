# We only need the ONNX Runtime C API *header* at compile time — the DLL/so is
# loaded at runtime via OrtGetApiBase (see src/stems/OrtDynamicApi.cpp), so we
# never link an import library. Fetch the header from the pinned release.
include(FetchContent)

set(PABLO_ORT_VERSION "1.20.1")

if(WIN32)
    set(_ort_pkg "onnxruntime-win-x64-${PABLO_ORT_VERSION}")
    set(_ort_url "https://github.com/microsoft/onnxruntime/releases/download/v${PABLO_ORT_VERSION}/${_ort_pkg}.zip")
else()
    set(_ort_pkg "onnxruntime-linux-x64-${PABLO_ORT_VERSION}")
    set(_ort_url "https://github.com/microsoft/onnxruntime/releases/download/v${PABLO_ORT_VERSION}/${_ort_pkg}.tgz")
endif()

FetchContent_Declare(ort_release URL ${_ort_url})
FetchContent_MakeAvailable(ort_release)

set(PABLO_ORT_INCLUDE_DIR "${ort_release_SOURCE_DIR}/include" CACHE PATH "" FORCE)
set(PABLO_ORT_LIB_DIR "${ort_release_SOURCE_DIR}/lib" CACHE PATH "" FORCE)
