#pragma once

#include <QWidget>
#include <QPushButton>
#include <QVBoxLayout>
#include <QLabel>
#include "gui_types.h"

class WelcomeWidget : public QWidget {
    Q_OBJECT
public:
    explicit WelcomeWidget(QWidget* parent = nullptr);

signals:
    void newSchematicRequested(AppMode mode);
};
