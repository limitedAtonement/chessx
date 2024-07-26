#ifndef CENTIPAWNGRAPH_H
#define CENTIPAWNGRAPH_H

#include <QWidget>
#include <QString>
#include "piece.h"
#include "gamex.h"
#include "gameevaluation.h"

class QLCDNumber;
class ChartWidget;
class QPushButton;

class CentipawnGraph final : public QWidget
{
    Q_OBJECT

public:
    CentipawnGraph(QWidget* parent = nullptr);
    void startAnalysis(GameX const &) noexcept;

signals:
    void requestPly(int);
    void startAnalysisRequested();

public slots:
    void slotDisplayCurrentPly(int ply);
    void slotDisplayMaterial(const QList<double>& material);
    void slotDisplayEvaluations(const QList<double>& evaluations);
    void evaluationChanged(std::unordered_map<MoveId, double> const &) noexcept;
    void evaluationComplete() noexcept;

private:
    ChartWidget* m_chart;
    QPushButton* m_startAnalysis;
    std::unique_ptr<GameEvaluation> evaluation;
    GameX currentGame;
    QList<double> scores;

private slots:
    void analysisRequested(bool) noexcept;
};

#endif
