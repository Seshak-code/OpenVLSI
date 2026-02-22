#include "postproc_engine.h"
#include <QtMath>
#include <QDebug>
#include <QRegularExpression>

namespace acutesim::compute::postproc {

PostProcEngine::PostProcEngine() {}

void PostProcEngine::setWaveforms(const QMap<QString, Waveform>& waves) {
    waves_ = waves;
}

void PostProcEngine::setFamilies(const QMap<QString, WaveFamily>& families) {
    families_ = families;
}

// ─── AST Parsing (Simple Recursive Descent for now) ──────────────────────────
// For Phase 1, we'll implement a very tailored parser or use a simple regex splitter
// A full parser (shunting-yard or recursive descent) is needed for "dB20(v(out)+1)"
// Here we assume simple function calls for prototype parity.
// TODO: Replace with full parser.

std::shared_ptr<AstNode> parseExpression(const QString& expr) {
    // Placeholder: parses "func(arg)" or "var" or "number"
    auto node = std::make_shared<AstNode>();
    
    QString e = expr.trimmed();
    if (e.contains("(")) {
        int open = e.indexOf("(");
        int close = e.lastIndexOf(")");
        node->type = AstNode::Function;
        node->value = e.left(open);
        
        // Split args (naive, doesn't handle nested commas)
        QString argsStr = e.mid(open + 1, close - open - 1);
        auto child = parseExpression(argsStr); // Single arg support only for V1 shim
        node->children.push_back(child);
    } 
    else if (e.startsWith("\"")) {
         node->type = AstNode::Literal; // String literal for node names v("/out")
         node->value = e.mid(1, e.length()-2);
    }
    else {
        bool ok;
        e.toDouble(&ok);
        if (ok) {
            node->type = AstNode::Literal;
            node->value = e;
        } else {
            node->type = AstNode::Identifier;
            node->value = e;
        }
    }
    return node;
}

Operand PostProcEngine::evaluate(const QString& expr) {
    auto ast = parseExpression(expr);
    return evaluate(ast);
}

Operand PostProcEngine::evaluate(const std::shared_ptr<AstNode>& node) {
    Operand res;
    res.type = OperandType::Scalar;
    
    if (node->type == AstNode::Literal) {
        // Check if string literal or number
        bool ok;
        double d = node->value.toDouble(&ok);
        if (ok) res.scalarVal = d;
        else {
             // String literal passed to function? e.g. v("/n1")
             // The v() function handles the lookup, so here we return the string?
             // Actually, identifiers usually resolve to data.
             // Literals are just values.
        }
    }
    else if (node->type == AstNode::Identifier) {
        // Variable lookup? For now, no variables in calc language
    }
    else if (node->type == AstNode::Function) {
        std::vector<Operand> args;
        for (auto& c : node->children) {
            // Special handling for v("/node"): The arg is a string literal
            if (node->value == "v" || node->value == "i") {
                // v() acts as lookup
                QString nodeName = c->value; 
                if (waves_.contains(nodeName)) {
                    Operand w;
                    w.type = OperandType::Waveform;
                    w.wave = waves_[nodeName];
                    args.push_back(w);
                } else {
                     // Error or empty
                }
            } else {
                args.push_back(evaluate(c));
            }
        }
        
        if (node->value != "v" && node->value != "i")
            return dispatchFunction(node->value, args);
        else if (!args.empty()) return args[0]; // v() returns its lookup result
    }
    
    return res;
}

// ─── Dispatch ────────────────────────────────────────────────────────────────

Operand PostProcEngine::dispatchFunction(const QString& func, const std::vector<Operand>& args) {
    if (args.empty()) return {};
    const auto& arg = args[0];
    
    Operand result;
    
    // Vectorized Math Dispatcher
    auto applyVec = [&](std::function<double(double)> op) {
        if (arg.type == OperandType::Waveform) {
            result.type = OperandType::Waveform;
            result.wave = mathFunc(arg.wave, op);
            result.wave.name = QString("%1(%2)").arg(func, arg.wave.name);
        } else if (arg.type == OperandType::Scalar) {
            result.type = OperandType::Scalar;
            result.scalarVal = op(arg.scalarVal);
        }
    };
    
    if (func == "dB20") {
        applyVec([](double x) { return 20.0 * std::log10(std::abs(x) + 1e-12); });
        result.wave.unit = "dB";
    }
    else if (func == "mag") {
        applyVec([](double x) { return std::abs(x); });
    }
    else if (func == "phase") {
        // Needs complex support, shim for now
        applyVec([](double) { return 0.0; }); 
    }
    else if (func == "average") {
        if (arg.type == OperandType::Waveform) {
            result.type = OperandType::Scalar;
            double sum = 0;
            for(double v : arg.wave.values) sum += v;
            result.scalarVal = arg.wave.values.isEmpty() ? 0 : sum / arg.wave.values.size();
        }
    }
    
    return result;
}

// ─── Vector Ops ──────────────────────────────────────────────────────────────

Waveform PostProcEngine::mathFunc(const Waveform& a, std::function<double(double)> op) {
    Waveform out;
    out.time = a.time; // Preserve time base
    out.unit = a.unit; 
    out.values.resize(a.values.size());
    
    // Explicit vectorization loop
    // Phase 2: Use SIMD/AVX here (via xsimd or similar)
    const double* src = a.values.constData();
    double* dst = out.values.data();
    int N = a.values.size();
    
    for (int i=0; i<N; ++i) {
        dst[i] = op(src[i]);
    }
    
    return out;
}

} // namespace acutesim::compute::postproc
