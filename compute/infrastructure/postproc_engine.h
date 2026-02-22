#pragma once
#include <vector>
#include <string>
#include <memory>
#include <complex>
#include <QVector>
#include <QString>
#include <QMap>

namespace acutesim::compute::postproc {

// ─── Data Types ──────────────────────────────────────────────────────────────

struct Waveform {
    QVector<double> time;
    QVector<double> values;
    // For AC:
    QVector<std::complex<double>> complexValues;
    bool isComplex = false;
    
    QString name;
    QString unit;
};

struct WaveFamily {
    QVector<Waveform> members;
    QVector<double> sweepValues; // e.g. Rload=1k, 2k, 3k
    QString sweepParam;          // "Rload"
    
    bool isEmpty() const { return members.isEmpty(); }
};

enum class OperandType { Scalar, Waveform, Family, ComplexScalar };

struct Operand {
    OperandType type;
    double scalarVal = 0.0;
    std::complex<double> complexVal = 0.0;
    Waveform wave;
    WaveFamily family;
};

// ─── AST Node ────────────────────────────────────────────────────────────────

struct AstNode {
    enum Type { 
        Literal,      // 3.14
        Identifier,   // v("/out")
        Function,     // dB20(...)
        Operator      // +, -, *, /
    } type;
    
    QString value; // func name or literal string
    std::vector<std::shared_ptr<AstNode>> children;
};

// ─── Engine ──────────────────────────────────────────────────────────────────

class PostProcEngine {
public:
    PostProcEngine();
    
    // Register data context (available waveforms)
    void setWaveforms(const QMap<QString, Waveform>& waves);
    void setFamilies(const QMap<QString, WaveFamily>& families);
    
    // Evaluate expression string
    Operand evaluate(const QString& expr);
    
    // Evaluate pre-built AST
    Operand evaluate(const std::shared_ptr<AstNode>& node);

private:
    QMap<QString, Waveform> waves_;
    QMap<QString, WaveFamily> families_;
    
    // Core math dispatch
    Operand dispatchFunction(const QString& func, const std::vector<Operand>& args);
    Operand dispatchOperator(const QString& op, const Operand& a, const Operand& b);
    
    // Vectorized Operations
    Waveform mathOp(const Waveform& a, const Waveform& b, std::function<double(double,double)> op);
    Waveform mathFunc(const Waveform& a, std::function<double(double)> op);
};

} // namespace acutesim::compute::postproc
