#include "proto_mapper.h"
#include <stdexcept>

namespace acutesim::compute::orchestration::ProtoMapper {

acutesim::simulation::CornerParameters toProto(const CornerParametersDTO& dto) {
    acutesim::simulation::CornerParameters msg;
    msg.set_name(dto.name);
    msg.set_temperature(dto.temperature);
    msg.set_supply_voltage(dto.supplyVoltage);
    msg.set_voltage_scale(dto.voltageScale);
    msg.set_process_corner(dto.processCorner);
    return msg;
}

CornerParametersDTO fromProto(const acutesim::simulation::CornerParameters& msg) {
    CornerParametersDTO dto;
    dto.name = msg.name();
    dto.temperature = msg.temperature();
    dto.supplyVoltage = msg.supply_voltage();
    dto.voltageScale = msg.voltage_scale();
    dto.processCorner = msg.process_corner();
    return dto;
}

// ── Netlist Transport ────────────────────────────────────────────────────────

acutesim::simulation::NetlistFormat toProto(NetlistFormatDTO dto) {
    return static_cast<acutesim::simulation::NetlistFormat>(dto);
}

NetlistFormatDTO fromProto(acutesim::simulation::NetlistFormat msg) {
    return static_cast<NetlistFormatDTO>(msg);
}

acutesim::simulation::SimulationSource toProto(const SimulationSourceDTO& dto) {
    acutesim::simulation::SimulationSource msg;
    msg.set_format(toProto(dto.format));
    msg.set_payload(dto.payload);
    return msg;
}

SimulationSourceDTO fromProto(const acutesim::simulation::SimulationSource& msg) {
    SimulationSourceDTO dto;
    dto.format = fromProto(msg.format());
    dto.payload = msg.payload();
    return dto;
}


acutesim::simulation::SimulationRequest toProto(const SimulationRequestDTO& dto) {
    acutesim::simulation::SimulationRequest msg;
    msg.set_session_token(dto.sessionToken);
    *msg.mutable_source() = toProto(dto.source);
    msg.set_analysis(static_cast<acutesim::simulation::AnalysisType>(dto.analysis));
    *msg.mutable_corner() = toProto(dto.corner);
    msg.set_topology_hash(dto.topologyHash);
    for (const auto& [k, v] : dto.parameters) {
        (*msg.mutable_parameters())[k] = v;
    }
    return msg;
}

SimulationRequestDTO fromProto(const acutesim::simulation::SimulationRequest& msg) {
    SimulationRequestDTO dto;
    dto.sessionToken = msg.session_token();
    dto.source = fromProto(msg.source());
    dto.analysis = static_cast<AnalysisTypeDTO>(msg.analysis());
    dto.corner = fromProto(msg.corner());
    dto.topologyHash = msg.topology_hash();
    for (const auto& [k, v] : msg.parameters()) {
        dto.parameters[k] = v;
    }
    return dto;
}

acutesim::simulation::ParameterVariation toProto(const ParameterVariationDTO& dto) {
    acutesim::simulation::ParameterVariation msg;
    msg.set_component_name(dto.componentName);
    msg.set_property_name(dto.propertyName);
    msg.set_override_value(dto.overrideValue);
    return msg;
}

ParameterVariationDTO fromProto(const acutesim::simulation::ParameterVariation& msg) {
    ParameterVariationDTO dto;
    dto.componentName = msg.component_name();
    dto.propertyName = msg.property_name();
    dto.overrideValue = msg.override_value();
    return dto;
}

acutesim::simulation::BatchRun toProto(const BatchRunDTO& dto) {
    acutesim::simulation::BatchRun msg;
    msg.set_run_index(dto.runIndex);
    for (const auto& var : dto.overrides) {
        *msg.add_overrides() = toProto(var);
    }
    return msg;
}

BatchRunDTO fromProto(const acutesim::simulation::BatchRun& msg) {
    BatchRunDTO dto;
    dto.runIndex = msg.run_index();
    for (int i = 0; i < msg.overrides_size(); ++i) {
        dto.overrides.push_back(fromProto(msg.overrides(i)));
    }
    return dto;
}

acutesim::simulation::SimulationBatchRequest toProto(const SimulationBatchRequestDTO& dto) {
    acutesim::simulation::SimulationBatchRequest msg;
    msg.set_session_token(dto.sessionToken);
    *msg.mutable_source() = toProto(dto.source);
    msg.set_analysis(static_cast<acutesim::simulation::AnalysisType>(dto.analysis));
    *msg.mutable_corner() = toProto(dto.corner);
    for (const auto& [k, v] : dto.parameters) {
        (*msg.mutable_parameters())[k] = v;
    }
    for (const auto& run : dto.runs) {
        *msg.add_runs() = toProto(run);
    }
    return msg;
}

SimulationBatchRequestDTO fromProto(const acutesim::simulation::SimulationBatchRequest& msg) {
    SimulationBatchRequestDTO dto;
    dto.sessionToken = msg.session_token();
    dto.source = fromProto(msg.source());
    dto.analysis = static_cast<AnalysisTypeDTO>(msg.analysis());
    dto.corner = fromProto(msg.corner());
    for (const auto& [k, v] : msg.parameters()) {
        dto.parameters[k] = v;
    }
    for (int i = 0; i < msg.runs_size(); ++i) {
        dto.runs.push_back(fromProto(msg.runs(i)));
    }
    return dto;
}

acutesim::simulation::MonteCarloVariation toProto(const MonteCarloVariationDTO& dto) {
    acutesim::simulation::MonteCarloVariation msg;
    msg.set_component_name(dto.componentName);
    msg.set_property_name(dto.propertyName);
    msg.set_distribution_type(dto.distributionType);
    msg.set_nominal_value(dto.nominalValue);
    msg.set_tolerance(dto.tolerance);
    return msg;
}

MonteCarloVariationDTO fromProto(const acutesim::simulation::MonteCarloVariation& msg) {
    MonteCarloVariationDTO dto;
    dto.componentName = msg.component_name();
    dto.propertyName = msg.property_name();
    dto.distributionType = msg.distribution_type();
    dto.nominalValue = msg.nominal_value();
    dto.tolerance = msg.tolerance();
    return dto;
}

acutesim::simulation::MonteCarloConfig toProto(const MonteCarloConfigDTO& dto) {
    acutesim::simulation::MonteCarloConfig msg;
    msg.set_sample_count(dto.sampleCount);
    msg.set_seed(dto.seed);
    for (const auto& var : dto.variations) {
        *msg.add_variations() = toProto(var);
    }
    return msg;
}

MonteCarloConfigDTO fromProto(const acutesim::simulation::MonteCarloConfig& msg) {
    MonteCarloConfigDTO dto;
    dto.sampleCount = msg.sample_count();
    dto.seed = msg.seed();
    for (int i = 0; i < msg.variations_size(); ++i) {
        dto.variations.push_back(fromProto(msg.variations(i)));
    }
    return dto;
}

acutesim::simulation::ConvergenceTelemetry toProto(const ConvergenceTelemetryDTO& dto) {
    acutesim::simulation::ConvergenceTelemetry msg;
    msg.set_iterations(dto.iterations);
    msg.set_max_residual(dto.maxResidual);
    msg.set_method(dto.method);
    msg.set_gmin_stepping_used(dto.gminSteppingUsed);
    msg.set_source_stepping_used(dto.sourceSteppingUsed);
    msg.set_solve_time_ms(dto.solveTimeMs);
    return msg;
}

ConvergenceTelemetryDTO fromProto(const acutesim::simulation::ConvergenceTelemetry& msg) {
    ConvergenceTelemetryDTO dto;
    dto.iterations = msg.iterations();
    dto.maxResidual = msg.max_residual();
    dto.method = msg.method();
    dto.gminSteppingUsed = msg.gmin_stepping_used();
    dto.sourceSteppingUsed = msg.source_stepping_used();
    dto.solveTimeMs = msg.solve_time_ms();
    return dto;
}

acutesim::simulation::SimulationResult toProto(const SimulationResponseDTO& dto) {
    acutesim::simulation::SimulationResult msg;
    // sessionToken is handled at the envelope level, leave it empty or populate if needed
    msg.set_success(dto.success);
    msg.set_error_message(dto.errorMessage);
    msg.set_run_index(dto.runIndex);
    
    for (double v : dto.timeVector) msg.add_time_vector(v);
    for (double v : dto.dcOperatingPoint) msg.add_dc_operating_point(v);

    for (const auto& [k, vec] : dto.nodeVoltages) {
        acutesim::simulation::TimeSeries ts;
        for (double v : vec) ts.add_values(v);
        (*msg.mutable_node_voltages())[k] = ts;
    }
    for (const auto& [k, vec] : dto.probeValues) {
        acutesim::simulation::TimeSeries ts;
        for (double v : vec) ts.add_values(v);
        (*msg.mutable_probe_values())[k] = ts;
    }
    *msg.mutable_convergence() = toProto(dto.convergence);
    return msg;
}

SimulationResponseDTO fromProto(const acutesim::simulation::SimulationResult& msg) {
    SimulationResponseDTO dto;
    dto.success = msg.success();
    dto.errorMessage = msg.error_message();
    dto.runIndex = msg.run_index();
    
    for (int i = 0; i < msg.time_vector_size(); ++i) dto.timeVector.push_back(msg.time_vector(i));
    for (int i = 0; i < msg.dc_operating_point_size(); ++i) dto.dcOperatingPoint.push_back(msg.dc_operating_point(i));

    for (const auto& pair : msg.node_voltages()) {
        std::vector<double> vec;
        for (double v : pair.second.values()) vec.push_back(v);
        dto.nodeVoltages[pair.first] = std::move(vec);
    }
    for (const auto& pair : msg.probe_values()) {
        std::vector<double> vec;
        for (double v : pair.second.values()) vec.push_back(v);
        dto.probeValues[pair.first] = std::move(vec);
    }
    dto.convergence = fromProto(msg.convergence());
    // Branch currents are omitted intentionally per protocol mapping
    return dto;
}

acutesim::simulation::StatisticalSummary toProto(const StatisticalSummaryDTO& dto) {
    acutesim::simulation::StatisticalSummary msg;
    for (const auto& [k,v] : dto.mean) (*msg.mutable_mean())[k] = v;
    for (const auto& [k,v] : dto.stddev) (*msg.mutable_stddev())[k] = v;
    for (const auto& [k,v] : dto.minVal) (*msg.mutable_min_val())[k] = v;
    for (const auto& [k,v] : dto.maxVal) (*msg.mutable_max_val())[k] = v;
    return msg;
}

StatisticalSummaryDTO fromProto(const acutesim::simulation::StatisticalSummary& msg) {
    StatisticalSummaryDTO dto;
    for (const auto& pair : msg.mean()) dto.mean[pair.first] = pair.second;
    for (const auto& pair : msg.stddev()) dto.stddev[pair.first] = pair.second;
    for (const auto& pair : msg.min_val()) dto.minVal[pair.first] = pair.second;
    for (const auto& pair : msg.max_val()) dto.maxVal[pair.first] = pair.second;
    return dto;
}

acutesim::simulation::BatchSummary toProtoBatchSummary(const SimulationBatchResponseDTO& dto) {
    acutesim::simulation::BatchSummary msg;
    msg.set_session_token(dto.sessionToken);
    msg.set_had_failures(dto.hadFailures);
    msg.set_total_runs(dto.runResults.size());
    int success = 0;
    for (const auto& result : dto.runResults) {
        if (result.success) success++;
    }
    msg.set_successful_runs(success);
    *msg.mutable_statistics() = toProto(dto.statistics);
    return msg;
}

SimulationBatchResponseDTO fromProtoBatchSummary(const acutesim::simulation::BatchSummary& msg) {
    SimulationBatchResponseDTO dto;
    dto.sessionToken = msg.session_token();
    dto.hadFailures = msg.had_failures();
    dto.statistics = fromProto(msg.statistics());
    // runResults are streamed out-of-band individually per standard contract
    return dto;
}

} // namespace acutesim::compute::orchestration::ProtoMapper
