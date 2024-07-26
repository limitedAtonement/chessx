#include "centipawngraph.h"

#include <QLCDNumber>
#include <QGridLayout>
#include <iostream>
#include <QPainter>
#include <QPushButton>

#include "chartwidget.h"
#include "gameevaluation.h"

CentipawnGraph::CentipawnGraph(QWidget* parent)
    : QWidget(parent)
    , m_chart(nullptr)
    , m_startAnalysis{nullptr}
    , evaluation{nullptr}
{
    setObjectName("CentipawnGraph");
    QGridLayout * layout = new QGridLayout(this);
    m_startAnalysis = new QPushButton{"Start"};
    m_startAnalysis->setDefault(true);
    connect(m_startAnalysis, &QPushButton::clicked, this, &CentipawnGraph::analysisRequested);
    layout->addWidget(m_startAnalysis, 0, 0);
    m_chart = new ChartWidget();
    m_chart->setObjectName("ChartWidget");
    m_chart->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    layout->addWidget(m_chart, 1, 0);
    m_chart->show();
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
    //std::cout << "  minimum size " << minimumSize().width() << ',' << minimumSize().height() << '\n';
    //std::cout << "  maximum size " << maximumSize().width() << ',' << maximumSize().height() << '\n';
    //std::cout << "  size hint " << sizeHint().width() << ',' << sizeHint().height() << '\n';
    //std::cout << "  minimumsize hint " << minimumSizeHint().width() << ',' << minimumSizeHint().height() << '\n';
    updateGeometry();
    //std::cout << "CentipawnGraph slotdisplaycurrentply " << ply << "\n";
    m_chart->setPly(ply);
    m_chart->show();
    m_chart->show();
}

void CentipawnGraph::slotDisplayMaterial(const QList<double>& material)
{
    //std::cout << "CentipawnGraph slotDisplayMaterial " << material.size() << "\n";
    m_chart->setValues(0, material);
}

void CentipawnGraph::slotDisplayEvaluations(const QList<double>& evaluations)
{
    // If we don't have a computed evaluation, use this one retrieved from annotations.
    //std::cout << "CentipawnGraph slotDisplayEvaluations " << evaluations.size() << "\n";
    m_chart->setValues(1, evaluations);
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
    try
    {
        if (evaluation)
        {
            std::cout << "Already have evaluation, returning\n";
            return;
        }
        evaluation = std::make_unique<GameEvaluation>(0, 1000, game);
        connect(evaluation.get(), &GameEvaluation::evaluationChanged, this, &CentipawnGraph::evaluationChanged);
        connect(evaluation.get(), &GameEvaluation::evaluationComplete, this, &CentipawnGraph::evaluationComplete);
        currentGame = game;
        scores.clear();
        std::cout << "centipawn starting analysis with " << currentGame.cursor().countMoves() << '\n';
        std::cout << "    expecting " << currentGame.cursor().countMoves() + 1 << " evalutaions!\n";
        scores.reserve(currentGame.cursor().countMoves()+1);
        for (int i{0}; i < currentGame.cursor().countMoves()+1; ++i)
            scores << 0;
        evaluation->start();
    }
    catch (...)
    {
        std::cout << "Failed to start evaluation\n";
        // Failed to start evaluation; swallowing error
    }
}

void CentipawnGraph::evaluationComplete() noexcept
{
    evaluation = nullptr;
}

void CentipawnGraph::evaluationChanged(std::unordered_map<int, double> const & scoreUpdates) noexcept
{
    GameX tempGame{currentGame};
    tempGame.moveToStart();
    std::cout << "got " << scoreUpdates.size() << " score updates...";
    for (std::pair<int, double> const & score : scoreUpdates)
    {
        int moveNumber = score.first;
        std::cout << moveNumber << "(" << score.first << "):" << score.second << ";";
        if (scores.size() <= moveNumber)
        {
            std::cout << "OUT OF RANGE\n";
            continue;
        }
        scores.replace(moveNumber, score.second);
    }
    std::cout << "\nnew scores:";
    std::cout.setf(std::ios::fixed);
    std::cout.precision(2);
    for (double s : scores)
    {
        std::cout << s << ',';
    }
    std::cout << '\n';
    m_chart->setValues(1, scores);
}

