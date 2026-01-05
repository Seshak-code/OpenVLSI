#include <iostream>
#include <string>
#include "mainwindow.h"
#include "stress_test.h"
#include "monte_carlo.h"
#include "benchmark_suite.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    if (argc > 1) 
    {
        std::string arg = argv[1];
        if (arg == "--stress") 
        {
            int numTrials = 100;
            if (argc > 2) numTrials = std::stoi(argv[2]);
            StressHarness::run(numTrials);
            return 0;
        }
        else if (arg == "--monte-carlo")
        {
            MonteCarloRunner::run();
            return 0;
        }
        else if (arg == "--benchmark")
        {
            GoldBench::run();
            return 0;
        }
    }

    QApplication app(argc, argv);
    
    // Qt styling
    app.setStyle("Fusion");

    MainWindow w;
    w.show();

    return app.exec();
}
