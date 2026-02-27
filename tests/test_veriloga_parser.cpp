// ============================================================================
// test_veriloga_parser.cpp
// ============================================================================
// Unit tests for the custom Verilog-A AST pipeline (Phase C7).
// Verifies Lexing -> Parsing -> Semantic Analysis -> Graph Lowering
// into deterministic TensorBlock entities.
// ============================================================================

#include <QtTest>
#include <QString>
#include <memory>
#include "physics/veriloga/lexer.h"
#include "physics/veriloga/parser.h"
#include "physics/veriloga/analyzer.h"
#include "physics/veriloga/graph_assembler.h"

using namespace acutesim::physics::veriloga;

class TestVerilogAParser : public QObject {
    Q_OBJECT

private slots:

    void testBasicResistor() {
        std::string source = R"(
module test_res(n1, gnd);
    electrical n1, gnd;
    parameter real R1_R = 1000.0;
    analog begin
        I(n1, gnd) <+ V(n1, gnd) / R1_R;
    end
endmodule
        )";

        Lexer lexer(source);
        auto tokens = lexer.tokenize();
        
        Parser parser(tokens);
        auto ast = parser.parseModule();
        
        Analyzer analyzer;
        analyzer.analyze(*ast);
        
        QCOMPARE(analyzer.parameters.size(), 1);
        QCOMPARE(analyzer.parameters["R1_R"], 1000.0);
        QVERIFY(analyzer.netIndices.count("n1"));
        QCOMPARE(analyzer.netIndices["gnd"], 0); // Hardcoded ground
        
        GraphAssembler assembler(analyzer);
        assembler.assemble(*ast);
        auto block = assembler.getResult();
        
        QCOMPARE(block.resistors.size(), 1);
        QCOMPARE(block.resistors[0].resistance_ohms, 1000.0);
        QCOMPARE(block.resistors[0].nodeTerminal2, 0); // gnd evaluates to 0
    }

    void testBasicCapacitor() {
        std::string source = R"(
module test_cap(n1, gnd);
    electrical n1, gnd;
    parameter real C1_C = 1e-12;
    analog begin
        I(n1, gnd) <+ C1_C * ddt(V(n1, gnd));
    end
endmodule
        )";

        Lexer lexer(source);
        Parser parser(lexer.tokenize());
        auto ast = parser.parseModule();
        
        Analyzer analyzer;
        analyzer.analyze(*ast);
        
        GraphAssembler assembler(analyzer);
        assembler.assemble(*ast);
        auto block = assembler.getResult();
        
        QCOMPARE(block.capacitors.size(), 1);
        QCOMPARE(block.capacitors[0].capacitance_farads, 1e-12);
    }

    void testMosfetSubcircuit() {
        std::string source = R"(
module test_mos(nd, ng, ns, nb);
    electrical nd, ng, ns, nb;
    parameter integer M1_isPMOS = 0; // NMOS
    parameter real M1_W = 1.5e-6;
    parameter real M1_L = 0.5e-6;

    analog begin
        begin : M1_sh
            real M1_Vgs;
            real M1_Vds;
            real M1_Vbs;
            I(nd, ns) <+ 0; // Stand-in for drain-source current
            M1_Vgs = V(ng, ns);
            M1_Vds = V(nd, ns);
            M1_Vbs = V(nb, ns);
        end
    end
endmodule
        )";

        Lexer lexer(source);
        auto tokens = lexer.tokenize();
        Parser parser(tokens);
        auto ast = parser.parseModule();
        
        Analyzer analyzer;
        analyzer.analyze(*ast);
        
        GraphAssembler assembler(analyzer);
        assembler.assemble(*ast);
        auto block = assembler.getResult();
        
        QCOMPARE(block.mosfets.size(), 1);
        QCOMPARE(block.mosfets[0].instanceName, std::string("M1"));
        QCOMPARE(block.mosfets[0].w, 1.5e-6);
        QCOMPARE(block.mosfets[0].l, 0.5e-6);
        QCOMPARE(block.mosfets[0].modelName, std::string("nfet_01v8")); // Detected from isPMOS=0
    }
};

QTEST_MAIN(TestVerilogAParser)
#include "test_veriloga_parser.moc"
