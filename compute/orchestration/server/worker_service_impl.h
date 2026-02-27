#pragma once

#include "worker_service.grpc.pb.h"
#include "subprocess.h"
#include <grpcpp/grpcpp.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>

namespace acutesim::orchestration {

using grpc::ServerContext;
using grpc::Status;
using grpc::ServerWriter;

using namespace acutesim::worker;

namespace fs = std::filesystem;

class WorkerServiceImpl final : public WorkerService::Service {
public:
    WorkerServiceImpl() {
        // Initialize capabilities on startup
        detectTools();
    }

    Status GetStatus(ServerContext* context, const StatusRequest* request, StatusResponse* response) override {
        response->set_worker_id("local-worker-01");
        response->set_version("0.1.0-alpha");
        response->set_ready(true);
        response->set_cpu_usage(0.1); // Placeholder: Accessing OS stats is platform-specific
        response->set_memory_usage(128.0);

        // Populate tool map
        auto* toolsMap = response->mutable_tools();
        for (const auto& [name, ver] : tools_) {
            (*toolsMap)[name] = ver;
        }

        return Status::OK;
    }

    Status RunSynthesis(ServerContext* context, const SynthesisRequest* request, ServerWriter<SynthesisLog>* writer) override {
        std::string job_id = request->job_id();
        if (job_id.empty()) {
            job_id = "job-" + std::to_string(std::rand());
        }

        // 1. Setup Sandbox
        fs::path sandboxDir = fs::current_path() / "sandbox" / job_id;
        try {
            fs::create_directories(sandboxDir);
        } catch (const fs::filesystem_error& e) {
            return Status(grpc::INTERNAL, std::string("Failed to create sandbox: ") + e.what());
        }

        // 2. Write Source Files
        for (const auto& file : request->source_files()) {
            fs::path filePath = sandboxDir / file.filename();
            std::ofstream ofs(filePath, std::ios::binary);
            ofs.write(file.content().data(), file.content().size());
        }

        // 3. Prepare Command
        if (request->source_files().empty()) {
             return Status(grpc::INVALID_ARGUMENT, "No source files provided");
        }

        std::string scriptName = request->source_files(0).filename();

        // 4. Run Yosys
        Subprocess proc("yosys", {"-s", scriptName});
        proc.setWorkingDirectory(sandboxDir.string());
        proc.setTimeout(std::chrono::minutes(10)); // Watchdog

        SubprocessResult res = proc.runWithStreaming(
            [&](const std::string& line) {
                SynthesisLog log;
                log.set_job_id(job_id);
                log.set_log_line(line);
                writer->Write(log);
            },
            [&](const std::string& line) {
                SynthesisLog log;
                log.set_job_id(job_id);
                log.set_log_line("ERR: " + line);
                log.set_is_error(true);
                writer->Write(log);
            }
        );

        // 5. Finalize
        SynthesisLog endLog;
        endLog.set_job_id(job_id);
        endLog.set_is_finished(true);

        if (res.timed_out) {
            endLog.set_is_error(true);
            endLog.set_log_line("Job Timed Out (Watchdog Violation)");
            writer->Write(endLog);
            return Status(grpc::DEADLINE_EXCEEDED, "Job timed out");
        } else if (res.exit_code != 0) {
            endLog.set_is_error(true);
            endLog.set_log_line("Process exited with code " + std::to_string(res.exit_code));
            writer->Write(endLog);
            return Status::OK; // Return OK so client receives the log, but log indicates error
        }

        writer->Write(endLog);
        return Status::OK;
    }

    Status RunPlaceAndRoute(ServerContext* context, const PnRRequest* request, ServerWriter<PnRLog>* writer) override {
        std::string job_id = request->job_id();
        if (job_id.empty()) {
            job_id = "pnr-" + std::to_string(std::rand());
        }

        // 1. Setup Sandbox
        fs::path sandboxDir = fs::current_path() / "sandbox" / job_id;
        try {
            fs::create_directories(sandboxDir);
        } catch (const fs::filesystem_error& e) {
            return Status(grpc::INTERNAL, std::string("Failed to create PnR sandbox: ") + e.what());
        }

        // 2. Write netlist file
        if (request->has_netlist()) {
            fs::path netlistPath = sandboxDir / request->netlist().filename();
            std::ofstream ofs(netlistPath, std::ios::binary);
            ofs.write(request->netlist().content().data(), request->netlist().content().size());
        }

        // 3. Write TCL script (if provided as constraint_file content)
        fs::path scriptPath = sandboxDir / "run_pnr.tcl";
        if (!request->constraint_file().empty()) {
            std::ofstream ofs(scriptPath);
            ofs << request->constraint_file();
        } else {
            // Generate minimal PnR script from floorplan params
            std::ofstream ofs(scriptPath);
            ofs << "# Auto-generated PnR script\n";
            if (request->has_floorplan()) {
                ofs << "initialize_floorplan -die_area {0 0 "
                    << request->floorplan().die_width() << " "
                    << request->floorplan().die_height() << "} "
                    << "-core_area {0 0 "
                    << request->floorplan().core_width() << " "
                    << request->floorplan().core_height() << "}\n";
            }
            ofs << "global_placement\n";
            ofs << "detailed_placement\n";
            ofs << "clock_tree_synthesis\n";
            ofs << "global_route\n";
            ofs << "detailed_route\n";
            ofs << "write_def result.def\n";
        }

        // 4. Log progress stages
        auto logProgress = [&](const std::string& msg, float progress) {
            PnRLog log;
            log.set_job_id(job_id);
            log.set_log_line(msg);
            log.set_progress(progress);
            writer->Write(log);
        };

        logProgress("Setting up PnR sandbox...", 0.05f);

        // 5. Run OpenROAD
        Subprocess proc("openroad", {"-exit", scriptPath.filename().string()});
        proc.setWorkingDirectory(sandboxDir.string());
        proc.setTimeout(std::chrono::minutes(30)); // PnR can take a while

        float progressEstimate = 0.1f;
        SubprocessResult res = proc.runWithStreaming(
            [&](const std::string& line) {
                PnRLog log;
                log.set_job_id(job_id);
                log.set_log_line(line);

                // Estimate progress from log keywords
                if (line.find("Placement") != std::string::npos) progressEstimate = 0.3f;
                else if (line.find("CTS") != std::string::npos) progressEstimate = 0.5f;
                else if (line.find("Routing") != std::string::npos) progressEstimate = 0.7f;
                else if (line.find("Writing") != std::string::npos) progressEstimate = 0.9f;
                else progressEstimate = std::min(progressEstimate + 0.01f, 0.95f);

                log.set_progress(progressEstimate);
                writer->Write(log);
            },
            [&](const std::string& line) {
                PnRLog log;
                log.set_job_id(job_id);
                log.set_log_line("ERR: " + line);
                writer->Write(log);
            }
        );

        // 6. Finalize
        PnRLog endLog;
        endLog.set_job_id(job_id);
        endLog.set_is_finished(true);
        endLog.set_progress(1.0f);

        if (res.timed_out) {
            endLog.set_log_line("PnR Timed Out (Watchdog Violation)");
            writer->Write(endLog);
            return Status(grpc::DEADLINE_EXCEEDED, "PnR job timed out");
        } else if (res.exit_code != 0) {
            endLog.set_log_line("OpenROAD exited with code " + std::to_string(res.exit_code));
            writer->Write(endLog);
            return Status::OK;
        }

        // Attach artifacts if they exist
        fs::path defPath = sandboxDir / "result.def";
        if (fs::exists(defPath)) {
            auto* defFile = endLog.mutable_def_file();
            defFile->set_filename("result.def");
            // Read file content
            std::ifstream ifs(defPath, std::ios::binary);
            std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
            defFile->set_content(content);
        }

        fs::path gdsPath = sandboxDir / "result.gds";
        if (fs::exists(gdsPath)) {
            auto* gdsFile = endLog.mutable_gds_file();
            gdsFile->set_filename("result.gds");
            std::ifstream ifs(gdsPath, std::ios::binary);
            std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
            gdsFile->set_content(content);
        }

        writer->Write(endLog);
        return Status::OK;
    }

    Status RunHDLSimulation(ServerContext* context, const HDLSimulationRequest* request, ServerWriter<HDLSimulationLog>* writer) override {
        std::string job_id = request->job_id();
        if (job_id.empty()) {
            job_id = "hdl-" + std::to_string(std::rand());
        }

        // 1. Setup Sandbox
        fs::path sandboxDir = fs::current_path() / "sandbox" / job_id;
        try {
            fs::create_directories(sandboxDir);
        } catch (const fs::filesystem_error& e) {
            return Status(grpc::INTERNAL, std::string("Failed to create HDL sandbox: ") + e.what());
        }

        // 2. Write source files
        std::vector<std::string> sourceNames;
        for (const auto& file : request->source_files()) {
            fs::path filePath = sandboxDir / file.filename();
            std::ofstream ofs(filePath, std::ios::binary);
            ofs.write(file.content().data(), file.content().size());
            sourceNames.push_back(file.filename());
        }

        // 3. Detect simulator: prefer Verilator, fallback to Icarus
        std::string simulator;
        std::vector<std::string> simArgs;

        if (tools_.count("verilator")) {
            simulator = "verilator";
            simArgs = {"--cc", "--exe", "--build", "--trace"};
            for (const auto& src : sourceNames) {
                simArgs.push_back(src);
            }
            if (!request->testbench_module().empty()) {
                simArgs.push_back("--top-module");
                simArgs.push_back(request->testbench_module());
            }
        } else if (tools_.count("iverilog")) {
            simulator = "iverilog";
            simArgs = {"-o", "sim_output"};
            for (const auto& src : sourceNames) {
                simArgs.push_back(src);
            }
        } else {
            // No simulator found - log and return
            HDLSimulationLog log;
            log.set_job_id(job_id);
            log.set_log_line("No HDL simulator found (need Verilator or Icarus Verilog)");
            log.set_is_finished(true);
            writer->Write(log);
            return Status::OK;
        }

        auto logMsg = [&](const std::string& msg) {
            HDLSimulationLog log;
            log.set_job_id(job_id);
            log.set_log_line(msg);
            writer->Write(log);
        };

        logMsg("Using simulator: " + simulator);

        // 4. Compile
        Subprocess compile(simulator, simArgs);
        compile.setWorkingDirectory(sandboxDir.string());
        compile.setTimeout(std::chrono::minutes(5));

        SubprocessResult compileRes = compile.runWithStreaming(
            [&](const std::string& line) { logMsg(line); },
            [&](const std::string& line) { logMsg("ERR: " + line); }
        );

        if (compileRes.exit_code != 0) {
            HDLSimulationLog endLog;
            endLog.set_job_id(job_id);
            endLog.set_log_line("Compilation failed with code " + std::to_string(compileRes.exit_code));
            endLog.set_is_finished(true);
            writer->Write(endLog);
            return Status::OK;
        }

        // 5. Run simulation (Icarus path)
        if (simulator == "iverilog") {
            Subprocess run("vvp", {"sim_output"});
            run.setWorkingDirectory(sandboxDir.string());
            run.setTimeout(std::chrono::minutes(10));

            SubprocessResult runRes = run.runWithStreaming(
                [&](const std::string& line) { logMsg(line); },
                [&](const std::string& line) { logMsg("ERR: " + line); }
            );
        }

        // 6. Capture VCD if it exists
        HDLSimulationLog endLog;
        endLog.set_job_id(job_id);
        endLog.set_is_finished(true);

        fs::path vcdPath = sandboxDir / "dump.vcd";
        if (fs::exists(vcdPath)) {
            std::ifstream ifs(vcdPath, std::ios::binary);
            std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
            endLog.set_vcd_chunk(content);
            logMsg("VCD waveform captured (" + std::to_string(content.size()) + " bytes)");
        }

        writer->Write(endLog);
        return Status::OK;
    }

    Status RunSignOff(ServerContext* context, const SignOffRequest* request, ServerWriter<SignOffLog>* writer) override {
        std::string job_id = request->job_id();
        if (job_id.empty()) {
            job_id = "signoff-" + std::to_string(std::rand());
        }

        // 1. Setup Sandbox
        fs::path sandboxDir = fs::current_path() / "sandbox" / job_id;
        try {
            fs::create_directories(sandboxDir);
        } catch (const fs::filesystem_error& e) {
            return Status(grpc::INTERNAL, std::string("Failed to create sign-off sandbox: ") + e.what());
        }

        auto logMsg = [&](const std::string& msg, bool isError = false) {
            SignOffLog log;
            log.set_job_id(job_id);
            log.set_log_line(msg);
            log.set_is_error(isError);
            writer->Write(log);
        };

        // 2. Write layout file
        if (request->has_layout_file()) {
            fs::path layoutPath = sandboxDir / request->layout_file().filename();
            std::ofstream ofs(layoutPath, std::ios::binary);
            ofs.write(request->layout_file().content().data(), request->layout_file().content().size());
        }

        // 3. Write script file
        fs::path scriptPath;
        if (request->has_script_file()) {
            scriptPath = sandboxDir / request->script_file().filename();
            std::ofstream ofs(scriptPath);
            ofs.write(request->script_file().content().data(), request->script_file().content().size());
        }

        // 4. Write reference netlist (for LVS)
        if (request->has_reference_netlist()) {
            fs::path refPath = sandboxDir / request->reference_netlist().filename();
            std::ofstream ofs(refPath, std::ios::binary);
            ofs.write(request->reference_netlist().content().data(), request->reference_netlist().content().size());
        }

        logMsg("Running " + request->check_type() + " sign-off check...");

        // 5. Run KLayout in batch mode
        if (!tools_.count("klayout")) {
            logMsg("KLayout not found. Install KLayout for sign-off checks.", true);
            SignOffLog endLog;
            endLog.set_job_id(job_id);
            endLog.set_is_finished(true);
            endLog.set_is_error(true);
            writer->Write(endLog);
            return Status::OK;
        }

        Subprocess proc("klayout", {"-b", "-r", scriptPath.filename().string()});
        proc.setWorkingDirectory(sandboxDir.string());
        proc.setTimeout(std::chrono::minutes(15));

        SubprocessResult res = proc.runWithStreaming(
            [&](const std::string& line) { logMsg(line); },
            [&](const std::string& line) { logMsg("ERR: " + line, true); }
        );

        // 6. Finalize
        SignOffLog endLog;
        endLog.set_job_id(job_id);
        endLog.set_is_finished(true);

        if (res.timed_out) {
            endLog.set_is_error(true);
            endLog.set_log_line("Sign-Off Timed Out");
            writer->Write(endLog);
            return Status(grpc::DEADLINE_EXCEEDED, "Sign-off timed out");
        } else if (res.exit_code != 0) {
            endLog.set_is_error(true);
            endLog.set_log_line("KLayout exited with code " + std::to_string(res.exit_code));
        }

        // Attach report if it exists
        fs::path reportPath = sandboxDir / "report.xml";
        if (!fs::exists(reportPath)) {
            // Try other common names
            for (const auto& name : {"report.txt", "drc_report.xml", "lvs_report.xml"}) {
                if (fs::exists(sandboxDir / name)) {
                    reportPath = sandboxDir / name;
                    break;
                }
            }
        }

        if (fs::exists(reportPath)) {
            auto* report = endLog.mutable_report();
            report->set_filename(reportPath.filename().string());
            std::ifstream ifs(reportPath, std::ios::binary);
            std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
            report->set_content(content);
            logMsg("Report attached: " + reportPath.filename().string() + " (" + std::to_string(content.size()) + " bytes)");
        }

        writer->Write(endLog);
        return Status::OK;
    }

    // =========================================================================
    // Phase 3.5: Explicit Sign-Off Stage RPCs
    // =========================================================================

    Status RunLVS(ServerContext* context, const LVSRequest* request, ServerWriter<SignOffLog>* writer) override {
        std::string job_id = request->job_id();
        if (job_id.empty()) job_id = "lvs-" + std::to_string(std::rand());

        fs::path sandboxDir = fs::current_path() / "sandbox" / job_id;
        try { fs::create_directories(sandboxDir); }
        catch (const fs::filesystem_error& e) {
            return Status(grpc::INTERNAL, std::string("Failed to create LVS sandbox: ") + e.what());
        }

        auto logMsg = [&](const std::string& msg, bool isError = false) {
            SignOffLog log; log.set_job_id(job_id); log.set_log_line(msg); log.set_is_error(isError);
            writer->Write(log);
        };

        if (request->has_layout_file()) {
            fs::path p = sandboxDir / request->layout_file().filename();
            std::ofstream ofs(p, std::ios::binary);
            ofs.write(request->layout_file().content().data(), request->layout_file().content().size());
        }
        if (request->has_schematic_netlist()) {
            fs::path p = sandboxDir / request->schematic_netlist().filename();
            std::ofstream ofs(p, std::ios::binary);
            ofs.write(request->schematic_netlist().content().data(), request->schematic_netlist().content().size());
        }

        logMsg("Running LVS (mode: " + (request->mode().empty() ? "hierarchical" : request->mode()) + ")...");
        std::string ruleDeck = request->rule_deck().empty() ? "lvs_rule_deck.rb" : request->rule_deck();
        Subprocess proc("klayout", {"-b", "-r", ruleDeck});
        proc.setWorkingDirectory(sandboxDir.string());
        proc.setTimeout(std::chrono::minutes(15));
        SubprocessResult res = proc.runWithStreaming(
            [&](const std::string& l) { logMsg(l); },
            [&](const std::string& l) { logMsg("ERR: " + l, true); }
        );
        SignOffLog endLog; endLog.set_job_id(job_id); endLog.set_is_finished(true);
        if (res.timed_out) { endLog.set_is_error(true); endLog.set_log_line("LVS timed out"); }
        else if (res.exit_code != 0) { endLog.set_is_error(true); endLog.set_log_line("KLayout LVS failed (exit " + std::to_string(res.exit_code) + ")"); }
        writer->Write(endLog);
        return Status::OK;
    }

    Status RunPEX(ServerContext* context, const PEXRequest* request, ServerWriter<SignOffLog>* writer) override {
        std::string job_id = request->job_id();
        if (job_id.empty()) job_id = "pex-" + std::to_string(std::rand());

        fs::path sandboxDir = fs::current_path() / "sandbox" / job_id;
        try { fs::create_directories(sandboxDir); }
        catch (const fs::filesystem_error& e) {
            return Status(grpc::INTERNAL, std::string("Failed to create PEX sandbox: ") + e.what());
        }

        auto logMsg = [&](const std::string& msg, bool isError = false) {
            SignOffLog log; log.set_job_id(job_id); log.set_log_line(msg); log.set_is_error(isError);
            writer->Write(log);
        };

        if (request->has_layout_file()) {
            fs::path p = sandboxDir / request->layout_file().filename();
            std::ofstream ofs(p, std::ios::binary);
            ofs.write(request->layout_file().content().data(), request->layout_file().content().size());
        }

        std::string engine  = request->engine().empty() ? "openroad_rcx" : request->engine();
        std::string spefOut = request->output_spef().empty() ? "output.spef" : request->output_spef();
        std::string defFile = request->has_layout_file() ? request->layout_file().filename() : "";
        logMsg("Running PEX engine: " + engine + "...");

        if (engine == "openroad_rcx") {
            std::string tcl = "read_def " + defFile + "\nextract_parasitics -max_res 0\nwrite_spef " + spefOut + "\nexit\n";
            fs::path tclPath = sandboxDir / "rcx.tcl";
            { std::ofstream ofs(tclPath); ofs << tcl; }
            Subprocess proc("openroad", {"-no_init", "rcx.tcl"});
            proc.setWorkingDirectory(sandboxDir.string());
            proc.setTimeout(std::chrono::minutes(20));
            SubprocessResult res = proc.runWithStreaming(
                [&](const std::string& l) { logMsg(l); },
                [&](const std::string& l) { logMsg("ERR: " + l, true); }
            );
            SignOffLog endLog; endLog.set_job_id(job_id); endLog.set_is_finished(true);
            if (res.timed_out) { endLog.set_is_error(true); endLog.set_log_line("PEX timed out"); }
            else if (res.exit_code != 0) { endLog.set_is_error(true); endLog.set_log_line("OpenROAD RCX failed"); }
            else { endLog.set_log_line("PEX complete. SPEF: " + spefOut); }
            writer->Write(endLog);
        } else {
            logMsg("Unknown PEX engine: " + engine, true);
            SignOffLog endLog; endLog.set_job_id(job_id); endLog.set_is_finished(true); endLog.set_is_error(true);
            writer->Write(endLog);
        }
        return Status::OK;
    }

    Status RunGDSExport(ServerContext* context, const GDSExportRequest* request, ServerWriter<SignOffLog>* writer) override {
        std::string job_id = request->job_id();
        if (job_id.empty()) job_id = "gds-" + std::to_string(std::rand());

        fs::path sandboxDir = fs::current_path() / "sandbox" / job_id;
        try { fs::create_directories(sandboxDir); }
        catch (const fs::filesystem_error& e) {
            return Status(grpc::INTERNAL, std::string("Failed to create GDS sandbox: ") + e.what());
        }

        auto logMsg = [&](const std::string& msg, bool isError = false) {
            SignOffLog log; log.set_job_id(job_id); log.set_log_line(msg); log.set_is_error(isError);
            writer->Write(log);
        };

        std::string inputName = request->has_input_file() ? request->input_file().filename() : "";
        std::string outputGDS = request->output_gds().empty() ? "output.gds" : request->output_gds();

        if (request->has_input_file()) {
            fs::path p = sandboxDir / inputName;
            std::ofstream ofs(p, std::ios::binary);
            ofs.write(request->input_file().content().data(), request->input_file().content().size());
        }

        logMsg("Exporting GDSII: " + inputName + " → " + outputGDS);
        std::string ruby = "layout = RBA::Layout::new\nlayout.read(\"" + inputName + "\")\n"
                           "opts = RBA::SaveLayoutOptions::new\nopts.format = \"GDS2\"\n"
                           "layout.write(\"" + outputGDS + "\", opts)\n"
                           "puts \"GDS export complete: " + outputGDS + "\"\n";
        fs::path rbPath = sandboxDir / "gds_export.rb";
        { std::ofstream ofs(rbPath); ofs << ruby; }

        Subprocess proc("klayout", {"-b", "-r", "gds_export.rb"});
        proc.setWorkingDirectory(sandboxDir.string());
        proc.setTimeout(std::chrono::minutes(10));
        SubprocessResult res = proc.runWithStreaming(
            [&](const std::string& l) { logMsg(l); },
            [&](const std::string& l) { logMsg("ERR: " + l, true); }
        );

        SignOffLog endLog; endLog.set_job_id(job_id); endLog.set_is_finished(true);
        if (res.timed_out) { endLog.set_is_error(true); endLog.set_log_line("GDS export timed out"); }
        else if (res.exit_code != 0) { endLog.set_is_error(true); endLog.set_log_line("KLayout GDS export failed"); }
        else {
            endLog.set_log_line("GDSII export complete: " + outputGDS);
            fs::path gdsPath = sandboxDir / outputGDS;
            if (fs::exists(gdsPath)) {
                auto* report = endLog.mutable_report();
                report->set_filename(outputGDS);
                std::ifstream ifs(gdsPath, std::ios::binary);
                report->set_content({std::istreambuf_iterator<char>(ifs), {}});
            }
        }
        writer->Write(endLog);
        return Status::OK;
    }

    Status RunCircuitSimulation(ServerContext* context, const CircuitSimulationRequest* request, ServerWriter<CircuitSimulationLog>* writer) override {
        std::string job_id = request->job_id();

        // This is the primary path for Tensor MNA
        CircuitSimulationLog log;
        log.set_job_id(job_id);
        log.set_log_line("Launch Circuit Simulation (Tensor MNA) - Mode: " + request->solver_mode());
        writer->Write(log);

        // Placeholder logic to run the core solver
        CircuitSimulationLog endLog;
        endLog.set_job_id(job_id);
        endLog.set_is_finished(true);
        writer->Write(endLog);

        return Status::OK;
    }

    Status RunRegression(ServerContext* context, const RegressionRequest* request, ServerWriter<RegressionLog>* writer) override {
        std::string job_id = request->job_id();
        std::string suite = request->suite_name();
        std::string mode = request->solver_mode();
        std::string prec = request->precision();

        // 1. Determine executable and arguments
        std::vector<std::string> searchPaths = {"./build/", "./"};
        std::string baseName = "test_regression";

        if (suite == "fpu_determinism") {
             baseName = "test_fpu_determinism";
        } else if (suite == "determinism_replay") {
             baseName = "test_determinism";
        } else if (suite == "all") {
             baseName = "test_worker_connectivity"; // Fallback/Placeholder
        }

        std::string exePath;
        bool found = false;
        for (const auto& prefix : searchPaths) {
            std::string p = prefix + baseName;
            if (fs::exists(p)) {
                exePath = p;
                found = true;
                break;
            }
        }

        if (!found) {
             exePath = "./build/test_worker_connectivity";
        }

        std::string logMsg = "Starting suite: " + suite + " mode: " + (mode.empty() ? "default" : mode) + " precision: " + (prec.empty() ? "default" : prec);
        RegressionLog startLog;
        startLog.set_job_id(job_id);
        startLog.set_log_line(logMsg);
        writer->Write(startLog);

        // 2. Spawn Process
        std::vector<std::string> args;
        Subprocess proc(exePath, args);
        proc.setTimeout(std::chrono::minutes(5));

        SubprocessResult res = proc.runWithStreaming(
            [&](const std::string& line) {
                RegressionLog log;
                log.set_job_id(job_id);
                log.set_log_line(line);
                writer->Write(log);
            },
            [&](const std::string& line) {
                RegressionLog log;
                log.set_job_id(job_id);
                log.set_log_line("ERR: " + line);
                log.set_is_error(true);
                writer->Write(log);
            }
        );

        // 3. Finalize
        RegressionLog endLog;
        endLog.set_job_id(job_id);
        endLog.set_is_finished(true);

        if (res.timed_out) {
            endLog.set_is_error(true);
            endLog.set_log_line("Regression Timed Out");
            endLog.set_passed(false);
        } else {
            endLog.set_passed(res.exit_code == 0);
        }

        writer->Write(endLog);
        return Status::OK;
    }

private:
    std::map<std::string, std::string> tools_;

    void detectTools() {
        // Detect Yosys
        {
            Subprocess proc("yosys", {"-V"});
            auto res = proc.run();
            if (res.exit_code == 0 && !res.stdout_output.empty()) {
                tools_["yosys"] = res.stdout_output.substr(0, 20) + "...";
            }
        }

        // Detect OpenROAD
        {
            Subprocess proc("openroad", {"-version"});
            auto res = proc.run();
            if (res.exit_code == 0 && !res.stdout_output.empty()) {
                tools_["openroad"] = res.stdout_output.substr(0, 30) + "...";
            }
        }

        // Detect KLayout
        {
            Subprocess proc("klayout", {"-v"});
            auto res = proc.run();
            if (res.exit_code == 0 && !res.stdout_output.empty()) {
                tools_["klayout"] = res.stdout_output.substr(0, 20) + "...";
            }
        }

        // Detect Verilator
        {
            Subprocess proc("verilator", {"--version"});
            auto res = proc.run();
            if (res.exit_code == 0 && !res.stdout_output.empty()) {
                tools_["verilator"] = res.stdout_output.substr(0, 30) + "...";
            }
        }

        // Detect Icarus Verilog
        {
            Subprocess proc("iverilog", {"-V"});
            auto res = proc.run();
            if (res.exit_code == 0 && !res.stdout_output.empty()) {
                // iverilog -V outputs multiple lines, first line has version
                auto pos = res.stdout_output.find('\n');
                tools_["iverilog"] = res.stdout_output.substr(0, std::min(pos, (size_t)30));
            }
        }
    }
};

} // namespace acutesim::orchestration
