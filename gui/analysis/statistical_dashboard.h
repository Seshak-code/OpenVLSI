#pragma once

#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSplitter>
#include <QPainter>
#include <QMouseEvent>
#include <vector>
#include <string>
#include <cmath>
#include <algorithm>
#include <numeric>

/**
 * statistical_dashboard.h
 * 
 * Visual Workspace for Phase 1.1: Statistical Analysis
 * Contains:
 *   - YieldHistogramWidget: Visualize Monte Carlo result distribution
 *   - SensitivityParetoWidget: Bar chart ranking of parameter sensitivity
 *   - StatisticalDashboard: Container widget combining both
 */

// Forward declaration
namespace Analysis { struct SensitivityResult; }

// ============================================================================
// YIELD HISTOGRAM WIDGET
// ============================================================================

class YieldHistogramWidget : public QWidget {
    Q_OBJECT
    
public:
    explicit YieldHistogramWidget(QWidget* parent = nullptr) : QWidget(parent) {
        setMinimumSize(300, 200);
        setStyleSheet("background-color: #1e1e1e;");
    }
    
    // Set Monte Carlo result data
    void setData(const std::vector<double>& results, double specMin, double specMax) {
        results_ = results;
        specMin_ = specMin;
        specMax_ = specMax;
        
        if (results_.empty()) return;
        
        // Calculate statistics
        double sum = std::accumulate(results_.begin(), results_.end(), 0.0);
        mean_ = sum / results_.size();
        
        double sq_sum = std::inner_product(results_.begin(), results_.end(), results_.begin(), 0.0);
        stddev_ = std::sqrt(sq_sum / results_.size() - mean_ * mean_);
        
        // Establish histogram bins
        dataMin_ = *std::min_element(results_.begin(), results_.end());
        dataMax_ = *std::max_element(results_.begin(), results_.end());
        
        double range = dataMax_ - dataMin_;
        double binWidth = range / numBins_;
        
        bins_.assign(numBins_, 0);
        for (double v : results_) {
            int bin = static_cast<int>((v - dataMin_) / binWidth);
            bin = std::max(0, std::min(bin, numBins_ - 1));
            bins_[bin]++;
        }
        
        maxBinCount_ = *std::max_element(bins_.begin(), bins_.end());
        
        // Calculate yield (samples within spec)
        int passCount = 0;
        for (double v : results_) {
            if (v >= specMin_ && v <= specMax_) passCount++;
        }
        yield_ = 100.0 * passCount / results_.size();
        
        update();
    }
    
signals:
    void specClicked(double value);
    
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        
        int w = width();
        int h = height();
        int margin = 40;
        int chartW = w - 2 * margin;
        int chartH = h - 2 * margin;
        
        // Draw axes
        p.setPen(QPen(QColor("#888888"), 1));
        p.drawLine(margin, h - margin, w - margin, h - margin); // X-axis
        p.drawLine(margin, margin, margin, h - margin); // Y-axis
        
        if (bins_.empty() || maxBinCount_ == 0) return;
        
        double range = dataMax_ - dataMin_;
        double binWidth = static_cast<double>(chartW) / numBins_;
        
        // Draw bars
        for (int i = 0; i < numBins_; ++i) {
            double barHeight = (chartH * bins_[i]) / maxBinCount_;
            
            double binCenter = dataMin_ + (i + 0.5) * (range / numBins_);
            bool inSpec = (binCenter >= specMin_ && binCenter <= specMax_);
            
            QColor barColor = inSpec ? QColor("#4CAF50") : QColor("#F44336"); // Green/Red
            p.setBrush(barColor);
            p.setPen(Qt::NoPen);
            
            int x = margin + i * binWidth + 1;
            int y = h - margin - barHeight;
            p.drawRect(x, y, binWidth - 2, barHeight);
        }
        
        // Draw spec lines
        auto xFromValue = [&](double v) {
            return margin + chartW * (v - dataMin_) / range;
        };
        
        p.setPen(QPen(QColor("#FFEB3B"), 2, Qt::DashLine)); // Yellow dashed
        if (specMin_ >= dataMin_ && specMin_ <= dataMax_) {
            int xMin = xFromValue(specMin_);
            p.drawLine(xMin, margin, xMin, h - margin);
        }
        if (specMax_ >= dataMin_ && specMax_ <= dataMax_) {
            int xMax = xFromValue(specMax_);
            p.drawLine(xMax, margin, xMax, h - margin);
        }
        
        // Draw mean line
        p.setPen(QPen(QColor("#2196F3"), 2)); // Blue solid
        int xMean = xFromValue(mean_);
        p.drawLine(xMean, margin, xMean, h - margin);
        
        // Draw 3-sigma lines
        p.setPen(QPen(QColor("#9C27B0"), 1, Qt::DotLine)); // Purple dotted
        int x3SigmaLo = xFromValue(mean_ - 3 * stddev_);
        int x3SigmaHi = xFromValue(mean_ + 3 * stddev_);
        if (x3SigmaLo > margin) p.drawLine(x3SigmaLo, margin, x3SigmaLo, h - margin);
        if (x3SigmaHi < w - margin) p.drawLine(x3SigmaHi, margin, x3SigmaHi, h - margin);
        
        // Draw labels
        p.setPen(QColor("#FFFFFF"));
        p.setFont(QFont("monospace", 10));
        p.drawText(margin, 20, QString("Yield: %1% | μ=%2 | σ=%3")
            .arg(yield_, 0, 'f', 1)
            .arg(mean_, 0, 'g', 4)
            .arg(stddev_, 0, 'g', 3));
        
        // X-axis labels
        p.setFont(QFont("monospace", 8));
        p.drawText(margin, h - 10, QString::number(dataMin_, 'g', 3));
        p.drawText(w - margin - 30, h - 10, QString::number(dataMax_, 'g', 3));
    }
    
private:
    std::vector<double> results_;
    std::vector<int> bins_;
    int numBins_ = 25;
    int maxBinCount_ = 0;
    double dataMin_ = 0.0, dataMax_ = 1.0;
    double mean_ = 0.0, stddev_ = 0.0;
    double specMin_ = 0.0, specMax_ = 1.0;
    double yield_ = 0.0;
};

// ============================================================================
// SENSITIVITY PARETO WIDGET
// ============================================================================

struct SensitivityDisplayItem {
    QString componentName;
    QString paramName;
    double normSensitivity;
};

class SensitivityParetoWidget : public QWidget {
    Q_OBJECT
    
public:
    explicit SensitivityParetoWidget(QWidget* parent = nullptr) : QWidget(parent) {
        setMinimumSize(300, 200);
        setStyleSheet("background-color: #1e1e1e;");
    }
    
    void setData(const std::vector<SensitivityDisplayItem>& items) {
        items_ = items;
        
        // Sort by absolute normalized sensitivity
        std::sort(items_.begin(), items_.end(), [](const auto& a, const auto& b) {
            return std::abs(a.normSensitivity) > std::abs(b.normSensitivity);
        });
        
        // Limit to top 10
        if (items_.size() > 10) items_.resize(10);
        
        // Find max for scaling
        maxSens_ = 0.0;
        for (const auto& item : items_) {
            maxSens_ = std::max(maxSens_, std::abs(item.normSensitivity));
        }
        
        update();
    }
    
signals:
    void componentClicked(const QString& componentName);
    
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        
        int w = width();
        int h = height();
        int leftMargin = 80;
        int rightMargin = 20;
        int topMargin = 30;
        int bottomMargin = 20;
        
        int chartW = w - leftMargin - rightMargin;
        int chartH = h - topMargin - bottomMargin;
        
        // Title
        p.setPen(QColor("#FFFFFF"));
        p.setFont(QFont("sans-serif", 11, QFont::Bold));
        p.drawText(leftMargin, 18, "Sensitivity Ranking (Top 10)");
        
        if (items_.empty() || maxSens_ == 0.0) return;
        
        int barHeight = std::min(20, chartH / static_cast<int>(items_.size()) - 4);
        int ySpacing = chartH / static_cast<int>(items_.size());
        
        p.setFont(QFont("monospace", 9));
        
        for (size_t i = 0; i < items_.size(); ++i) {
            const auto& item = items_[i];
            int y = topMargin + i * ySpacing + ySpacing / 2 - barHeight / 2;
            
            // Draw label
            p.setPen(QColor("#BBBBBB"));
            p.drawText(5, y + barHeight / 2 + 4, item.componentName);
            
            // Draw bar
            double barWidthFrac = std::abs(item.normSensitivity) / maxSens_;
            int barW = static_cast<int>(barWidthFrac * chartW);
            
            QColor barColor = (item.normSensitivity > 0) ? QColor("#4CAF50") : QColor("#F44336");
            p.setBrush(barColor);
            p.setPen(Qt::NoPen);
            p.drawRoundedRect(leftMargin, y, barW, barHeight, 3, 3);
            
            // Draw value
            p.setPen(QColor("#FFFFFF"));
            QString valText = QString::number(item.normSensitivity, 'f', 2);
            p.drawText(leftMargin + barW + 5, y + barHeight / 2 + 4, valText);
        }
    }
    
    void mousePressEvent(QMouseEvent* event) override {
        // Simple hit detection
        int ySpacing = (height() - 50) / static_cast<int>(items_.size());
        int idx = (event->pos().y() - 30) / ySpacing;
        
        if (idx >= 0 && idx < static_cast<int>(items_.size())) {
            emit componentClicked(items_[idx].componentName);
        }
    }
    
private:
    std::vector<SensitivityDisplayItem> items_;
    double maxSens_ = 1.0;
};

// ============================================================================
// STATISTICAL DASHBOARD (Container)
// ============================================================================

class StatisticalDashboard : public QWidget {
    Q_OBJECT
    
public:
    explicit StatisticalDashboard(QWidget* parent = nullptr) : QWidget(parent) {
        setupUI();
    }
    
    YieldHistogramWidget* histogram() { return histogram_; }
    SensitivityParetoWidget* pareto() { return pareto_; }
    
signals:
    void highlightComponent(const QString& name);
    void exportRequested();
    
private:
    void setupUI() {
        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        
        // Header Area
        auto* header = new QWidget(this);
        auto* hLayout = new QHBoxLayout(header);
        
        auto* title = new QLabel("Statistical Analysis Dashboard");
        title->setStyleSheet("font-size: 14px; font-weight: bold; color: #2196F3; padding: 8px;");
        hLayout->addWidget(title);
        
        hLayout->addStretch();
        
        auto* exportBtn = new QPushButton("Export CSV", header);
        exportBtn->setStyleSheet("QPushButton { background: #333; color: #ccc; border: 1px solid #555; padding: 4px 10px; }"
                                 "QPushButton:hover { background: #444; }");
        connect(exportBtn, &QPushButton::clicked, this, &StatisticalDashboard::exportRequested);
        hLayout->addWidget(exportBtn);
        
        layout->addWidget(header);
        
        // Splitter for side-by-side or stacked view
        auto* splitter = new QSplitter(Qt::Horizontal, this);
        
        histogram_ = new YieldHistogramWidget(this);
        pareto_ = new SensitivityParetoWidget(this);
        
        splitter->addWidget(histogram_);
        splitter->addWidget(pareto_);
        splitter->setSizes({1, 1});
        
        layout->addWidget(splitter);
        
        // Connect pareto click to highlight signal
        connect(pareto_, &SensitivityParetoWidget::componentClicked,
                this, &StatisticalDashboard::highlightComponent);
    }
    
    YieldHistogramWidget* histogram_ = nullptr;
    SensitivityParetoWidget* pareto_ = nullptr;
};
