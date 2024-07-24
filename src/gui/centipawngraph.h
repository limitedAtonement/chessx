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
    ~CentipawnGraph() noexcept;

signals:
    void requestPly(int);
    void startAnalysisRequested();

public slots:
    void slotDisplayCurrentPly(int ply);
    void slotDisplayMaterial(const QList<double>& material);
    void slotDisplayEvaluations(const QList<double>& evaluations);
    void slotDisplayTime(const QString& timeWhite, const QString& timeBlack);
    void slotDisplayTime(Color color, const QString& time);
    void evaluationChanged(std::unordered_map<MoveId, double> const &) noexcept;
    void evaluationComplete() noexcept;

protected:
    QSize sizeHint() const override;

private:
    QLCDNumber* m_clock1;
    QLCDNumber* m_clock2;
    ChartWidget* m_chart;
    QPushButton* m_startAnalysis;
    GameEvaluation * evaluation;
    GameX currentGame;
    QList<double> scores;

private slots:
    void analysisRequested(bool) noexcept;
};

#endif
