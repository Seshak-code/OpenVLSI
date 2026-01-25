#pragma once
#include <string>
#include <vector>
#include <map>
#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <cmath>
#include <limits>

struct ReferenceResult {
    std::string testName;
    std::vector<double> nodeVoltages;
    double kcl_residual;
    int iterations;
    bool converged;
    uint64_t signature = 0; // Topology Hash + Config Hash
    std::map<std::string, std::string> metadata; // [NEW] Metadata fields
};

class RegressionDB {
private:
    std::map<std::string, ReferenceResult> db;
    std::string dbPath;

public:
    RegressionDB(const std::string& path = "regression_golden.db") : dbPath(path) {
        load();
    }

    static std::map<std::string, std::string> getSystemMetadata() {
        std::map<std::string, std::string> meta;
        #ifdef __APPLE__
            meta["os"] = "macOS";
        #elif _WIN32
            meta["os"] = "windows";
        #elif __linux__
            meta["os"] = "linux";
        #elif __EMSCRIPTEN__
            meta["os"] = "wasm";
        #else
            meta["os"] = "unknown";
        #endif

        #if defined(__clang__)
            meta["compiler"] = "clang";
        #elif defined(__GNUC__)
            meta["compiler"] = "gcc";
        #elif defined(_MSC_VER)
            meta["compiler"] = "msvc";
        #else
            meta["compiler"] = "unknown";
        #endif
        
        return meta;
    }

    void load() {
        std::ifstream file(dbPath);
        if (!file.is_open()) return;

        std::string line;
        while (std::getline(file, line)) {
            if (line.empty() || line[0] == '#') continue;
            
            std::stringstream ss(line);
            ReferenceResult res;
            std::string segment;
            
            // Format: Name|Converged|Iter|KCL|Signature|V1,V2...|MetaKey=Val,MetaKey2=Val2
            if (!std::getline(ss, res.testName, '|')) continue;
            
            int convInt;
            if (!std::getline(ss, segment, '|')) continue;
            convInt = std::stoi(segment);
            res.converged = (convInt != 0);

            if (!std::getline(ss, segment, '|')) continue;
            res.iterations = std::stoi(segment);

            if (!std::getline(ss, segment, '|')) continue;
            res.kcl_residual = std::stod(segment);

            std::string nextSeg;
            // Parse Signature
            if (std::getline(ss, nextSeg, '|')) {
                // Check if it's the voltages (legacy) or signature
                 if (nextSeg.find(',') == std::string::npos && !nextSeg.empty() && nextSeg.find('.') == std::string::npos) {
                     try { res.signature = std::stoull(nextSeg); } catch(...) { res.signature = 0; }
                     
                     // Parse Voltages
                     if (std::getline(ss, nextSeg, '|')) {
                         std::stringstream vss(nextSeg);
                         std::string vVal;
                         while (std::getline(vss, vVal, ',')) {
                             res.nodeVoltages.push_back(std::stod(vVal));
                         }
                     }
                     
                     // Parse Metadata (Optional) [NEW]
                     if (std::getline(ss, nextSeg, '|')) {
                        std::stringstream mss(nextSeg);
                        std::string pair;
                        while(std::getline(mss, pair, ',')) {
                            size_t eq = pair.find('=');
                            if (eq != std::string::npos) {
                                res.metadata[pair.substr(0, eq)] = pair.substr(eq + 1);
                            }
                        }
                     }

                 } else {
                     // Legacy format: Volts come directly
                    std::stringstream vss(nextSeg);
                    std::string vVal;
                    while (std::getline(vss, vVal, ',')) {
                        res.nodeVoltages.push_back(std::stod(vVal));
                    }
                    res.signature = 0;
                 }
            }
            db[res.testName] = res;
        }
    }

    void save() {
        std::ofstream file(dbPath);
        file << "# CircuitSim Regression Database v2\n";
        file << "# Name|Converged|Iter|KCL|Signature|Voltages|Metadata\n";
        
        for (const auto& kv : db) {
            const auto& res = kv.second;
            file << res.testName << "|"
                 << (res.converged ? 1 : 0) << "|"
                 << res.iterations << "|"
                 << std::scientific << std::setprecision(16) << res.kcl_residual << "|"
                 << res.signature << "|";
            
            for (size_t i = 0; i < res.nodeVoltages.size(); ++i) {
                file << res.nodeVoltages[i];
                if (i < res.nodeVoltages.size() - 1) file << ",";
            }
            file << "|";

            // Save Metadata
            size_t mCount = 0;
            for (const auto& meta : res.metadata) {
                file << meta.first << "=" << meta.second;
                if (mCount < res.metadata.size() - 1) file << ",";
                mCount++;
            }

            file << "\n";
        }
    }

    void update(const std::string& name, const std::vector<double>& voltages, double kcl, int iter, bool conv, uint64_t sig, const std::map<std::string, std::string>& meta = {}) {
        ReferenceResult res;
        res.testName = name;
        res.nodeVoltages = voltages;
        res.kcl_residual = kcl;
        res.iterations = iter;
        res.converged = conv;
        res.signature = sig;
        res.metadata = meta;
        db[name] = res;
    }

    const ReferenceResult* get(const std::string& name) const {
        auto it = db.find(name);
        if (it != db.end()) return &it->second;
        return nullptr;
    }

    // Compare new result with golden. Returns true if match.
    // tol: absolute tolerance for voltages
    bool compare(const std::string& name, const std::vector<double>& currentVoltages, uint64_t currentSig, double tol = 1e-6) const {
        const ReferenceResult* ref = get(name);
        if (!ref) {
            std::cout << "[NEW] No reference found for: " << name << "\n";
            return false;
        }

        if (ref->signature != 0 && currentSig != 0 && ref->signature != currentSig) {
            std::cout << "[WARN] Topology/Config mismatch for " << name << " (Hash " << ref->signature << " vs " << currentSig << ")\n";
        }

        if (ref->nodeVoltages.size() != currentVoltages.size()) {
             std::cerr << "[FAIL] Node count mismatch for " << name << ". Ref: " 
                       << ref->nodeVoltages.size() << ", Curr: " << currentVoltages.size() << "\n";
             return false;
        }

        bool match = true;
        double max_diff = 0.0;
        for (size_t i = 0; i < currentVoltages.size(); ++i) {
            double diff = std::abs(currentVoltages[i] - ref->nodeVoltages[i]);
            if (diff > max_diff) max_diff = diff;
            if (diff > tol) {
                if (match) { 
                    std::cerr << "[FAIL] " << name << " Voltage Mismatch (Max Diff: " << max_diff << " > " << tol << ")\n";
                }
                match = false;
            }
        }
        return match;
    }
};
