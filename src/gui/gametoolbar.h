#ifndef GAMETOOLBAR_H
#define GAMETOOLBAR_H

#include "piece.h"
#include "toolmainwindow.h"

class QLCDNumber;
class ChartWidget;

class GameToolBar : public QWidget
{
    Q_OBJECT

public:
    GameToolBar(QWidget* parent = nullptr);

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
