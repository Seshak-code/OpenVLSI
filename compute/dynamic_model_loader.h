#pragma once
// DynamicModelLoader — Hot-reload physics models via shared libraries.
//
// Workflow:
//   1. Physics Compiler generates a C++ header (gen_model.h).
//   2. A shim .cpp is generated that #includes the header.
//   3. The shim is compiled into a .dylib/.so via clang.
//   4. DynamicModelLoader::load() calls dlopen on the shared library.
//   5. The static initializer in the library registers the model with
//      ModelRegistry automatically.
//   6. On reload, the old library is unloaded and the new one is loaded.
//
// Safety:
//   - Models are loaded into a sandboxed namespace (anonymous namespace in
//     the generated code prevents symbol collisions).
//   - Old handles are cleaned up via dlclose before reload.
//   - Duplicate registration is guarded by ModelRegistry::registerModel().

#include <string>
#include <unordered_map>
#include <vector>
#include <iostream>

#ifdef __APPLE__
#include <dlfcn.h>
#elif defined(__linux__)
#include <dlfcn.h>
#elif defined(_WIN32)
#include <windows.h>
#endif

#include "model_registry.h"

namespace acutesim::compute {

class DynamicModelLoader {
public:
    static DynamicModelLoader& instance() {
        static DynamicModelLoader loader;
        return loader;
    }

    ~DynamicModelLoader() {
        unloadAll();
    }

    /// Load a shared library containing a compiled physics model.
    /// Returns true on success.
    /// If a library with the same modelName is already loaded, it is
    /// unloaded first (hot-reload).
    bool load(const std::string& modelName, const std::string& libraryPath) {
        // Unload previous version if exists
        auto it = loadedLibs_.find(modelName);
        if (it != loadedLibs_.end()) {
            std::cout << "[DynamicModelLoader] Unloading previous: "
                      << modelName << "\n";
            closeHandle(it->second);
            loadedLibs_.erase(it);
        }

        // Load new library
#ifdef _WIN32
        HMODULE handle = LoadLibraryA(libraryPath.c_str());
        if (!handle) {
            std::cerr << "[DynamicModelLoader] Failed to load: "
                      << libraryPath << " (error=" << GetLastError() << ")\n";
            return false;
        }
#else
        void* handle = dlopen(libraryPath.c_str(), RTLD_NOW | RTLD_LOCAL);
        if (!handle) {
            std::cerr << "[DynamicModelLoader] Failed to load: "
                      << libraryPath << "\n"
                      << "  dlerror: " << dlerror() << "\n";
            return false;
        }
#endif

        loadedLibs_[modelName] = handle;
        std::cout << "[DynamicModelLoader] Loaded: " << modelName
                  << " from " << libraryPath << "\n";

        // Registration happens automatically via the static initializer
        // in the loaded library (REGISTER_PHYSICS_MODEL_EX macro).
        return true;
    }

    /// Unload a previously loaded model library.
    bool unload(const std::string& modelName) {
        auto it = loadedLibs_.find(modelName);
        if (it == loadedLibs_.end()) {
            std::cerr << "[DynamicModelLoader] Not loaded: " << modelName << "\n";
            return false;
        }
        closeHandle(it->second);
        loadedLibs_.erase(it);
        std::cout << "[DynamicModelLoader] Unloaded: " << modelName << "\n";
        return true;
    }

    /// Unload all loaded model libraries.
    void unloadAll() {
        for (auto& [name, handle] : loadedLibs_) {
            closeHandle(handle);
        }
        loadedLibs_.clear();
    }

    /// Check if a model is currently loaded.
    bool isLoaded(const std::string& modelName) const {
        return loadedLibs_.count(modelName) > 0;
    }

    /// List all currently loaded model names.
    std::vector<std::string> listLoaded() const {
        std::vector<std::string> names;
        for (const auto& [name, _] : loadedLibs_) {
            names.push_back(name);
        }
        return names;
    }

private:
    DynamicModelLoader() = default;

    void closeHandle(void* handle) {
#ifdef _WIN32
        FreeLibrary(static_cast<HMODULE>(handle));
#else
        dlclose(handle);
#endif
    }

#ifdef _WIN32
    std::unordered_map<std::string, HMODULE> loadedLibs_;
#else
    std::unordered_map<std::string, void*> loadedLibs_;
#endif
};

} // namespace acutesim::compute
