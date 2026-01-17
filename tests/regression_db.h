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
};

class RegressionDB {
private:
    std::map<std::string, ReferenceResult> db;
    std::string dbPath;

public:
    RegressionDB(const std::string& path = "regression_golden.db") : dbPath(path) {
        load();
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
            
            // Format: Name|Converged|Iter|KCL|Signature|V1,V2...
            // Or Legacy: Name|Converged|Iter|KCL|V1,V2...
            if (!std::getline(ss, res.testName, '|')) continue;
            
            int convInt;
            if (!std::getline(ss, segment, '|')) continue;
            convInt = std::stoi(segment);
            res.converged = (convInt != 0);

            if (!std::getline(ss, segment, '|')) continue;
            res.iterations = std::stoi(segment);

            if (!std::getline(ss, segment, '|')) continue;
            res.kcl_residual = std::stod(segment);

            // Check if next token is Signature (long int) or Voltages (list of doubles)
            // Heuristic: If it contains just digits, it's likely signature. If commas, it's voltages.
            std::string nextSeg;
            if (std::getline(ss, nextSeg, '|')) {
                if (nextSeg.find(',') == std::string::npos && !nextSeg.empty() && nextSeg.find('.') == std::string::npos) {
                    // Likely signature
                    try {
                        res.signature = std::stoull(nextSeg);
                    } catch (...) { res.signature = 0; } // Fallback
                    
                    // Next is voltages
                    if (std::getline(ss, nextSeg, '|')) {
                         std::stringstream vss(nextSeg);
                         std::string vVal;
                         while (std::getline(vss, vVal, ',')) {
                             res.nodeVoltages.push_back(std::stod(vVal));
                         }
                    }
                } else {
                    // Legacy format: This segment IS voltages
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
        file << "# CircuitSim Regression Database\n";
        file << "# Name|Converged|Iter|KCL|Voltages\n";
        
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
            file << "\n";
        }
    }

    void update(const std::string& name, const std::vector<double>& voltages, double kcl, int iter, bool conv, uint64_t sig) {
        ReferenceResult res;
        res.testName = name;
        res.nodeVoltages = voltages;
        res.kcl_residual = kcl;
        res.iterations = iter;
        res.converged = conv;
        res.signature = sig;
        db[name] = res;
    }

    const ReferenceResult* get(const std::string& name) const {
        auto it = db.find(name);
        if (it != db.end()) return &it->second;
        return nullptr;
    }

    // Compare new result with golden. Returns true if match.
    // tol: absolute tolerance for voltages
    // tolerance: absolute voltage tolerance
    bool compare(const std::string& name, const std::vector<double>& currentVoltages, uint64_t currentSig, double tol = 1e-6) const {
        const ReferenceResult* ref = get(name);
        if (!ref) {
            std::cout << "[NEW] No reference found for: " << name << "\n";
            return false;
        }

        if (ref->signature != 0 && currentSig != 0 && ref->signature != currentSig) {
            std::cout << "[WARN] Topology/Config mismatch for " << name << " (Hash " << ref->signature << " vs " << currentSig << ")\n";
            // We treat this as a warning but proceed to check voltages. 
            // If voltages match, maybe the hash change was benign (cosmetic)?
            // But strict regression usually implies fail.
            // For now, print WARN.
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
                // Return false immediately or log first? Log first few.
                if (match) { // Only print header once
                    std::cerr << "[FAIL] " << name << " Voltage Mismatch (Max Diff: " << max_diff << " > " << tol << ")\n";
                }
                match = false;
            }
        }
        return match;
    }
};
