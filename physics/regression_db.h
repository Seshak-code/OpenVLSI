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
            
            // Format: Name|Converged|Iter|KCL|V1,V2,V3...
            if (!std::getline(ss, res.testName, '|')) continue;
            
            int convInt;
            if (!std::getline(ss, segment, '|')) continue;
            convInt = std::stoi(segment);
            res.converged = (convInt != 0);

            if (!std::getline(ss, segment, '|')) continue;
            res.iterations = std::stoi(segment);

            if (!std::getline(ss, segment, '|')) continue;
            res.kcl_residual = std::stod(segment);

            if (std::getline(ss, segment, '|')) {
                std::stringstream vss(segment);
                std::string vVal;
                while (std::getline(vss, vVal, ',')) {
                    res.nodeVoltages.push_back(std::stod(vVal));
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
                 << std::scientific << std::setprecision(16) << res.kcl_residual << "|";
            
            for (size_t i = 0; i < res.nodeVoltages.size(); ++i) {
                file << res.nodeVoltages[i];
                if (i < res.nodeVoltages.size() - 1) file << ",";
            }
            file << "\n";
        }
    }

    void update(const std::string& name, const std::vector<double>& voltages, double kcl, int iter, bool conv) {
        ReferenceResult res;
        res.testName = name;
        res.nodeVoltages = voltages;
        res.kcl_residual = kcl;
        res.iterations = iter;
        res.converged = conv;
        db[name] = res;
    }

    const ReferenceResult* get(const std::string& name) const {
        auto it = db.find(name);
        if (it != db.end()) return &it->second;
        return nullptr;
    }

    // Compare new result with golden. Returns true if match.
    // tol: absolute tolerance for voltages
    bool compare(const std::string& name, const std::vector<double>& currentVoltages, double tol = 1e-6) const {
        const ReferenceResult* ref = get(name);
        if (!ref) {
            std::cerr << "[WARN] No reference found for: " << name << "\n";
            return false;
        }

        if (ref->nodeVoltages.size() != currentVoltages.size()) {
             std::cerr << "[FAIL] Node count mismatch for " << name << ". Ref: " 
                       << ref->nodeVoltages.size() << ", Curr: " << currentVoltages.size() << "\n";
             return false;
        }

        bool match = true;
        for (size_t i = 0; i < currentVoltages.size(); ++i) {
            double diff = std::abs(currentVoltages[i] - ref->nodeVoltages[i]);
            if (diff > tol) {
                std::cerr << "[FAIL] " << name << " Node " << (i+1) << " mismatch. Ref: " 
                          << ref->nodeVoltages[i] << ", Curr: " << currentVoltages[i] 
                          << ", Diff: " << diff << "\n";
                match = false;
            }
        }
        return match;
    }
};
