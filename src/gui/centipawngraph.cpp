#include "centipawngraph.h"

#include <QLCDNumber>
#include <QGridLayout>
#include <iostream>
#include <QPainter>

#include "chartwidget.h"

CentipawnGraph::CentipawnGraph(QWidget* parent)
    : QWidget(parent)
    , m_clock1(nullptr)
    , m_clock2(nullptr)
    , m_chart(nullptr)
{
    setObjectName("CentipawnGraph");

    std::cout << "CentipawnGraph Constructor\n";
    QGridLayout * layout = new QGridLayout(this);
    m_clock1 = new QLCDNumber(7);
    m_clock1->setSegmentStyle(QLCDNumber::Flat);
    m_clock1->setObjectName("Clock0");
    m_clock1->display(15);
    layout->addWidget(m_clock1, 1, 0);
    m_clock1->show();

    m_chart = new ChartWidget();
    m_chart->setObjectName("ChartWidget");
    m_chart->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    layout->addWidget(m_chart, 1, 1);
    m_chart->show();

    m_clock2 = new QLCDNumber(7);
    m_clock2->setSegmentStyle(QLCDNumber::Flat);
    m_clock2->setObjectName("Clock1");
    m_clock2->display(166);
    std::cout << " clock 2 digit count " << m_clock2->digitCount() << '\n';
    //m_clock1->setStyleSheet("border: 1px solid red");
    layout->addWidget(m_clock2, 1, 2);
    //setStyleSheet("border: 1px solid blue");
    connect(m_chart, &ChartWidget::halfMoveRequested, this, &CentipawnGraph::requestPly);
    std::cout << "CentipawnGraph Constructor connected\n";
    adjustSize();
}

static void printDimensions(QRect const & re)
{
    std::cout << "  left " << re.left() << " top " << re.top() << " right " << re.right() << " bottom " << re.bottom() << '\n';
}

void CentipawnGraph::slotDisplayCurrentPly(int ply)
{
    std::cout << "CentipawnGraph layout geometry ";
    printDimensions(layout()->geometry());
    std::cout << "CentipawnGraph geometry        ";
    printDimensions(geometry());
    std::cout << "  minimum size " << minimumSize().width() << ',' << minimumSize().height() << '\n';
    std::cout << "  maximum size " << maximumSize().width() << ',' << maximumSize().height() << '\n';
    std::cout << "  size hint " << sizeHint().width() << ',' << sizeHint().height() << '\n';
    std::cout << "  minimumsize hint " << minimumSizeHint().width() << ',' << minimumSizeHint().height() << '\n';
    updateGeometry();
    //std::cout << "CentipawnGraph slotdisplaycurrentply " << ply << "\n";
    m_chart->setPly(ply);
    m_clock1->show();
    m_clock1->setVisible(true);
    m_chart->show();
    m_clock1->display("0:0:0");
    m_clock2->display("1:0:0");
    m_clock1->show();
    m_chart->show();
    std::cout << "  clock dimension ";
    printDimensions(m_clock1->frameGeometry());
    std::cout << "  clock 2dimension ";
    printDimensions(m_clock2->frameGeometry());
}

void CentipawnGraph::slotDisplayMaterial(const QList<double>& material)
{
    //std::cout << "CentipawnGraph slotDisplayMaterial " << material.size() << "\n";
    m_chart->setValues(0, material);
}

void CentipawnGraph::slotDisplayEvaluations(const QList<double>& evaluations)
{
    //std::cout << "CentipawnGraph slotDisplayEvaluations " << evaluations.size() << "\n";
    m_chart->setValues(1, evaluations);
}

void CentipawnGraph::slotDisplayTime(const QString& timeWhite, const QString &timeBlack)
{
    //std::cout << "CentipawnGraph slotDisplayTime: " << timeWhite.toStdString() << "\n";
    m_clock1->display(timeWhite);
    m_clock2->display(timeBlack);
}

void CentipawnGraph::slotDisplayTime(Color color, const QString& text)
{
    //std::cout << "CentipawnGraph slotDisplayTime with color: " << text.toStdString() << "\n";
    switch (color)
    {
    case White:
        m_clock1->display(text);
        break;
    case Black:
        m_clock2->display(text);
        break;
    default:
        break;
    }
}

QSize CentipawnGraph::sizeHint() const
{
    return {300, 101};
}
