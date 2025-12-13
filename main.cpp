#include <QApplication>
#include <QLabel>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    
    QLabel *label = new QLabel("Hello World!");
    label->setWindowTitle("Qt Hello World");
    label->setAlignment(Qt::AlignCenter);
    label->setMinimumSize(400, 200);
    label->setStyleSheet("font-size: 24px; font-weight: bold;");
    label->show();
    
    return app.exec();
}
