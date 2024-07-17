#ifndef CENTIPAWNGRAPH_H
#define CENTIPAWNGRAPH_H

#include <QWidget>
#include <QString>
#include "piece.h"

class QLCDNumber;
class ChartWidget;

class CentipawnGraph : public QWidget
{
    Q_OBJECT

public:
    CentipawnGraph(QWidget* parent = nullptr);

signals:
    void requestPly(int);

public slots:
    void slotDisplayCurrentPly(int ply);
    void slotDisplayMaterial(const QList<double>& material);
    void slotDisplayEvaluations(const QList<double>& evaluations);
    void slotDisplayTime(const QString& timeWhite, const QString& timeBlack);
    void slotDisplayTime(Color color, const QString& time);

protected:
    QSize sizeHint() const override;

private:
    QLCDNumber* m_clock1;
    QLCDNumber* m_clock2;
    ChartWidget* m_chart;
};

#endif
