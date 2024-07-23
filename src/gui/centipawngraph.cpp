#include "centipawngraph.h"

#include <QLCDNumber>
#include <QGridLayout>
#include <iostream>
#include <QPainter>
#include <QPushButton>
#include <thread>

#include "chartwidget.h"
#include "gameevaluation.h"

CentipawnGraph::CentipawnGraph(QWidget* parent)
    : QWidget(parent)
    , m_clock1(nullptr)
    , m_clock2(nullptr)
    , m_chart(nullptr)
    , m_startAnalysis{nullptr}
    , evaluation{nullptr}
{
    setObjectName("CentipawnGraph");

    //std::cout << "CentipawnGraph Constructor\n";
    QGridLayout * layout = new QGridLayout(this);
    m_startAnalysis = new QPushButton{"Start"};
    m_startAnalysis->setDefault(true);
    connect(m_startAnalysis, &QAbstractButton::clicked, this, &CentipawnGraph::analysisRequested);
    layout->addWidget(m_startAnalysis, 0, 0);

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
    //std::cout << " clock 2 digit count " << m_clock2->digitCount() << '\n';
    //m_clock1->setStyleSheet("border: 1px solid red");
    layout->addWidget(m_clock2, 1, 2);
    //setStyleSheet("border: 1px solid blue");
    connect(m_chart, &ChartWidget::halfMoveRequested, this, &CentipawnGraph::requestPly);
    //std::cout << "CentipawnGraph Constructor connected\n";
    adjustSize();
}

static void printDimensions(QRect const & /*re*/)
{
    //std::cout << "  left " << re.left() << " top " << re.top() << " right " << re.right() << " bottom " << re.bottom() << '\n';
}

void CentipawnGraph::slotDisplayCurrentPly(int ply)
{
    //std::cout << "CentipawnGraph layout geometry ";
    printDimensions(layout()->geometry());
    //std::cout << "CentipawnGraph geometry        ";
    printDimensions(geometry());
    //std::cout << "  minimum size " << minimumSize().width() << ',' << minimumSize().height() << '\n';
    //std::cout << "  maximum size " << maximumSize().width() << ',' << maximumSize().height() << '\n';
    //std::cout << "  size hint " << sizeHint().width() << ',' << sizeHint().height() << '\n';
    //std::cout << "  minimumsize hint " << minimumSizeHint().width() << ',' << minimumSizeHint().height() << '\n';
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
    //std::cout << "  clock dimension ";
    printDimensions(m_clock1->frameGeometry());
    //std::cout << "  clock 2dimension ";
    printDimensions(m_clock2->frameGeometry());
}

void CentipawnGraph::slotDisplayMaterial(const QList<double>& material)
{
    //std::cout << "CentipawnGraph slotDisplayMaterial " << material.size() << "\n";
    m_chart->setValues(0, material);
}

void CentipawnGraph::slotDisplayEvaluations(const QList<double>& evaluations)
{
    // If we don't have a compted evaluation, use this one retrieved from annotations.
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

void CentipawnGraph::analysisRequested(bool /*checked*/) noexcept
{
    //std::cout << "CentipawnGraph::analysisRequested...\n";
    // In order to start analysis, we need the current game which we don't have.
    // We need to emit a signal that the main window can handle so that the
    // main window can call back with the current game to get analysis started.
    emit startAnalysisRequested();
}

void CentipawnGraph::startAnalysis(GameX const & game) noexcept
{
    //std::cout << "CentipawnGraph::startAnalysis with game...\n";
    std::this_thread::sleep_for(std::chrono::milliseconds(25));
    try
    {
        if (evaluation)
        {
            std::cout << "Already have evaluation, returning\n";
            return;
        }
        evaluation = new GameEvaluation{0, 1000, game};
        connect(evaluation, &GameEvaluation::evaluationChanged, this, &CentipawnGraph::evaluationChanged);
        connect(evaluation, &GameEvaluation::evaluationComplete, this, &CentipawnGraph::evaluationComplete);
        //std::cout << "   calling start...\n";
        evaluation->start();
    }
    catch (...)
    {
        // Failed to start evaluation; swallowing error
    }
}

CentipawnGraph::~CentipawnGraph() noexcept
{
    if (evaluation)
        delete evaluation;
}

void CentipawnGraph::evaluationComplete() noexcept
{
    delete evaluation;
    evaluation = nullptr;
}

void CentipawnGraph::evaluationChanged(std::unordered_map<MoveId, double> const &) noexcept
{
    std::cout << "CentipawnGraph Got new evaluations\n";
}

