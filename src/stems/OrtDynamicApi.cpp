#include "OrtDynamicApi.h"

#if PABLO_ENABLE_STEMS

#include <onnxruntime_c_api.h>

#if JUCE_WINDOWS
 #define WIN32_LEAN_AND_MEAN
 #include <windows.h>
#else
 #include <dlfcn.h>
#endif

namespace pablo
{
namespace
{
    const OrtApi* loadOrtApi()
    {
        static const OrtApi* api = []() -> const OrtApi*
        {
            using GetApiBaseFn = const OrtApiBase* (ORT_API_CALL*)();
            GetApiBaseFn getApiBase = nullptr;

            // Search order: inside our own .vst3 bundle's Resources dir (CI
            // puts onnxruntime.dll there), then the user data dir, then the
            // system default search path.
            const auto module = juce::File::getSpecialLocation (juce::File::currentExecutableFile);
            const auto bundleResources = module.getParentDirectory()      // x86_64-win / x86_64-linux
                                               .getParentDirectory()      // Contents
                                               .getChildFile ("Resources");
            const auto appDataRuntime = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                                            .getChildFile ("PabloSampler").getChildFile ("runtime");

           #if JUCE_WINDOWS
            const char* libName = "onnxruntime.dll";
           #elif JUCE_MAC
            const char* libName = "libonnxruntime.dylib";
           #else
            const char* libName = "libonnxruntime.so";
           #endif

            juce::StringArray candidates {
                bundleResources.getChildFile (libName).getFullPathName(),
                module.getParentDirectory().getChildFile (libName).getFullPathName(),
                appDataRuntime.getChildFile (libName).getFullPathName(),
                juce::String (libName)
            };

            for (const auto& path : candidates)
            {
               #if JUCE_WINDOWS
                HMODULE handle = path.containsChar ('\\') || path.containsChar ('/')
                    ? LoadLibraryExW (path.toWideCharPointer(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH)
                    : LoadLibraryW (path.toWideCharPointer());
                if (handle != nullptr)
                    getApiBase = (GetApiBaseFn) (void*) GetProcAddress (handle, "OrtGetApiBase");
               #else
                if (void* handle = dlopen (path.toRawUTF8(), RTLD_NOW | RTLD_LOCAL))
                    getApiBase = (GetApiBaseFn) dlsym (handle, "OrtGetApiBase");
               #endif
                if (getApiBase != nullptr)
                    break;
            }

            if (getApiBase == nullptr)
                return nullptr;
            if (const auto* base = getApiBase())
                return base->GetApi (ORT_API_VERSION);
            return nullptr;
        }();
        return api;
    }

    juce::String ortErrorToString (const OrtApi& api, OrtStatus* status)
    {
        if (status == nullptr)
            return {};
        juce::String msg (api.GetErrorMessage (status));
        api.ReleaseStatus (status);
        return msg;
    }
} // namespace

struct OrtSession::Impl
{
    const OrtApi* api = nullptr;
    OrtEnv* env = nullptr;
    OrtSessionOptions* options = nullptr;
    ::OrtSession* session = nullptr;
    OrtMemoryInfo* memoryInfo = nullptr;
    juce::String inputName;
    std::vector<juce::String> outputNames;
    int numStems = 4;

    ~Impl()
    {
        if (api != nullptr)
        {
            if (session != nullptr)    api->ReleaseSession (session);
            if (options != nullptr)    api->ReleaseSessionOptions (options);
            if (memoryInfo != nullptr) api->ReleaseMemoryInfo (memoryInfo);
            if (env != nullptr)        api->ReleaseEnv (env);
        }
    }
};

OrtSession::OrtSession() : impl (std::make_unique<Impl>()) {}
OrtSession::~OrtSession() = default;

bool OrtSession::isRuntimeAvailable() { return loadOrtApi() != nullptr; }
int OrtSession::getNumStems() const { return impl->numStems; }

bool OrtSession::load (const juce::File& modelFile, juce::String& error)
{
    auto* api = loadOrtApi();
    if (api == nullptr)
    {
        error = "ONNX Runtime library not found";
        return false;
    }
    impl->api = api;

    if (auto msg = ortErrorToString (*api, api->CreateEnv (ORT_LOGGING_LEVEL_ERROR, "pablo", &impl->env)); msg.isNotEmpty())
        { error = msg; return false; }
    if (auto msg = ortErrorToString (*api, api->CreateSessionOptions (&impl->options)); msg.isNotEmpty())
        { error = msg; return false; }

    api->SetIntraOpNumThreads (impl->options, juce::jmax (1, juce::SystemStats::getNumCpus() - 1));
    api->SetSessionGraphOptimizationLevel (impl->options, ORT_ENABLE_ALL);

    const auto modelPathString = modelFile.getFullPathName();
   #if JUCE_WINDOWS
    const auto* modelPath = modelPathString.toWideCharPointer();
   #else
    const auto modelPathStr = modelPathString.toStdString();
    const auto* modelPath = modelPathStr.c_str();
   #endif

    if (auto msg = ortErrorToString (*api, api->CreateSession (impl->env, modelPath, impl->options, &impl->session)); msg.isNotEmpty())
        { error = "Failed to load model: " + msg; return false; }

    if (auto msg = ortErrorToString (*api, api->CreateCpuMemoryInfo (OrtArenaAllocator, OrtMemTypeDefault, &impl->memoryInfo)); msg.isNotEmpty())
        { error = msg; return false; }

    // Discover I/O names instead of hardcoding them, so different exports of
    // the model keep working.
    OrtAllocator* allocator = nullptr;
    api->GetAllocatorWithDefaultOptions (&allocator);

    size_t numInputs = 0, numOutputs = 0;
    api->SessionGetInputCount (impl->session, &numInputs);
    api->SessionGetOutputCount (impl->session, &numOutputs);
    if (numInputs < 1 || numOutputs < 1)
        { error = "Model has unexpected inputs/outputs"; return false; }

    char* name = nullptr;
    api->SessionGetInputName (impl->session, 0, allocator, &name);
    impl->inputName = juce::String (juce::CharPointer_UTF8 (name));
    allocator->Free (allocator, name);

    for (size_t i = 0; i < numOutputs; ++i)
    {
        char* outName = nullptr;
        api->SessionGetOutputName (impl->session, i, allocator, &outName);
        impl->outputNames.push_back (juce::String (juce::CharPointer_UTF8 (outName)));
        allocator->Free (allocator, outName);
    }
    return true;
}

bool OrtSession::run (const juce::AudioBuffer<float>& segment,
                      std::vector<juce::AudioBuffer<float>>& stemsOut,
                      juce::String& error)
{
    auto* api = impl->api;
    if (api == nullptr || impl->session == nullptr)
        { error = "Session not loaded"; return false; }

    const int numCh = segment.getNumChannels();
    const int numSamples = segment.getNumSamples();

    // Planar [1, ch, samples] float32 input, which is what the HTDemucs ONNX
    // exports expect.
    std::vector<float> inputData ((size_t) numCh * (size_t) numSamples);
    for (int ch = 0; ch < numCh; ++ch)
        std::memcpy (inputData.data() + (size_t) ch * (size_t) numSamples,
                     segment.getReadPointer (ch), sizeof (float) * (size_t) numSamples);

    const int64_t shape[3] = { 1, numCh, numSamples };
    OrtValue* inputTensor = nullptr;
    if (auto msg = ortErrorToString (*api, api->CreateTensorWithDataAsOrtValue (
            impl->memoryInfo, inputData.data(), inputData.size() * sizeof (float),
            shape, 3, ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, &inputTensor)); msg.isNotEmpty())
        { error = msg; return false; }

    const auto inputNameStr = impl->inputName.toStdString();
    const char* inputNames[] = { inputNameStr.c_str() };

    std::vector<std::string> outNameStrs;
    std::vector<const char*> outputNames;
    for (const auto& n : impl->outputNames)
    {
        outNameStrs.push_back (n.toStdString());
        outputNames.push_back (outNameStrs.back().c_str());
    }

    std::vector<OrtValue*> outputs (outputNames.size(), nullptr);
    auto msg = ortErrorToString (*api, api->Run (impl->session, nullptr,
                                                 inputNames, &inputTensor, 1,
                                                 outputNames.data(), outputNames.size(),
                                                 outputs.data()));
    api->ReleaseValue (inputTensor);
    if (msg.isNotEmpty())
        { error = "Inference failed: " + msg; return false; }

    // Find the tensor output holding the stems. Accept [1, stems, ch, S],
    // [stems, ch, S] or per-stem outputs of [1, ch, S] / [ch, S].
    stemsOut.clear();
    bool ok = false;

    auto releaseOutputs = [&]
    {
        for (auto* v : outputs)
            if (v != nullptr)
                api->ReleaseValue (v);
    };

    auto extract = [&] (OrtValue* value) -> bool
    {
        OrtTensorTypeAndShapeInfo* info = nullptr;
        if (api->GetTensorTypeAndShape (value, &info) != nullptr)
            return false;
        size_t numDims = 0;
        api->GetDimensionsCount (info, &numDims);
        std::vector<int64_t> dims (numDims);
        api->GetDimensions (info, dims.data(), numDims);
        api->ReleaseTensorTypeAndShapeInfo (info);

        // Drop leading batch dim of 1.
        size_t d = 0;
        while (numDims - d > 3 && dims[d] == 1) ++d;

        float* data = nullptr;
        if (api->GetTensorMutableData (value, (void**) &data) != nullptr || data == nullptr)
            return false;

        if (numDims - d == 3)   // [stems, ch, S]
        {
            const auto stems = (int) dims[d], ch = (int) dims[d + 1];
            const auto samples = (juce::int64) dims[d + 2];
            for (int s = 0; s < stems; ++s)
            {
                juce::AudioBuffer<float> stem (ch, (int) samples);
                for (int c = 0; c < ch; ++c)
                    stem.copyFrom (c, 0,
                                   data + ((size_t) s * (size_t) ch + (size_t) c) * (size_t) samples,
                                   (int) samples);
                stemsOut.push_back (std::move (stem));
            }
            return stems > 0;
        }
        if (numDims - d == 2)   // one stem per output: [ch, S]
        {
            const auto ch = (int) dims[d];
            const auto samples = (juce::int64) dims[d + 1];
            juce::AudioBuffer<float> stem (ch, (int) samples);
            for (int c = 0; c < ch; ++c)
                stem.copyFrom (c, 0, data + (size_t) c * (size_t) samples, (int) samples);
            stemsOut.push_back (std::move (stem));
            return true;
        }
        return false;
    };

    if (outputs.size() == 1)
        ok = extract (outputs[0]);
    else
        for (auto* v : outputs)
            ok = extract (v) || ok;

    releaseOutputs();

    if (! ok || stemsOut.empty())
        { error = "Unexpected model output shape"; return false; }

    impl->numStems = (int) stemsOut.size();
    return true;
}
} // namespace pablo

#else // !PABLO_ENABLE_STEMS

namespace pablo
{
struct OrtSession::Impl {};
OrtSession::OrtSession() = default;
OrtSession::~OrtSession() = default;
bool OrtSession::isRuntimeAvailable() { return false; }
int OrtSession::getNumStems() const { return 0; }
bool OrtSession::load (const juce::File&, juce::String& error) { error = "Stem splitting disabled in this build"; return false; }
bool OrtSession::run (const juce::AudioBuffer<float>&, std::vector<juce::AudioBuffer<float>>&, juce::String& error) { error = "Stem splitting disabled in this build"; return false; }
} // namespace pablo

#endif
