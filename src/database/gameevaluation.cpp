#include <QFile>
#include <iostream>
#include "enginex.h"
#include "gamex.h"
#include "gameevaluation.h"

GameEvaluation::GameEvaluation(int engineIndex, int msPerMove, GameX game) noexcept
    : engineIndex{engineIndex}
    , msPerMove{msPerMove}
    , game{game}
    , targetThreadCount{std::max(1, QThread::idealThreadCount())}
    , line{""}
    , moveNumbers{0}
{
    if (targetThreadCount > 4)
        // We'll leave one logical core idle if we can afford it.
        --targetThreadCount;
    connect(&timer, &QTimer::timeout, this, &GameEvaluation::update);
}

void GameEvaluation::start()
{
    if (running)
    {
        throw std::logic_error{"Game evaluation already running"};
    }
    std::cout << "Game evaluation starting with " << game.cursor().countMoves() << " moves\n";
    line = "";
    running = true;
    game.moveToStart();
    workers.clear();
    //workers.reserve(game.cursor().countMoves());
    timer.stop();
    timer.start(std::chrono::milliseconds{100});
    // We place the first worker for the starting position here.
    workers.emplace_back(engineIndex, game.startingBoard(), game.board(), msPerMove, game.currentMove(), line, moveNumbers++);
}

void GameEvaluation::update() noexcept
{
    std::unordered_map<int, double> evaluations;
    std::cerr << "Compiling scores...\n";
    for (std::list<GameEvaluationWorker>::iterator i{workers.begin()}; i != workers.end(); ++i)
    {
        i->update();
        evaluations.emplace(i->moveNumber, i->getLastScore());
        std::cerr << "   adding move " << i->getMove() << " score " << i->getLastScore() << " worker " << i->moveNumber << '\n';
        if (!i->isRunning())
        {
            // Erase *after* getting the value because erasing will destroy the worker
            i = workers.erase(i);
            --i;
        }
    }
    std::cerr << "GameEvaluation::update... workers size " << workers.size() << "\n";
    emit evaluationChanged(evaluations);
    while (static_cast<int>(workers.size()) < targetThreadCount)
    {
        if (!game.forward())
        {
            break;
        }
        try
        {
            int tempNum {moveNumbers++};
            std::cerr << "Creating worker " << tempNum << " with move " << game.currentMove() << "\n";
            workers.emplace_back(engineIndex, game.startingBoard(), game.board(), msPerMove, game.currentMove(), line, tempNum);
        }
        catch (...)
        {
            // Destroy any workers that have started.
            workers.clear();
            break;
        }
        // See gamecursor.cpp::moveToId(MoveId, QString*)
        line.push_back(game.move().toAlgebraic());
        line.push_back(" ");
    }
    if (!workers.size())
    {
        timer.stop();
        emit evaluationComplete();
        running = false;
    }
}

void GameEvaluation::stop() noexcept
{
    workers.clear();
    timer.stop();
    emit evaluationComplete();
}

GameEvaluation::~GameEvaluation() noexcept
{
    workers.clear();
    timer.stop();
}

GameEvaluationWorker::GameEvaluationWorker(int engineIndex, BoardX const & startPosition, BoardX const & currentPosition,
        int msPerMove, MoveId move, QString const & line, int moveNumber)
    : moveNumber{moveNumber}
    , startPosition{startPosition}
    , currentPosition{currentPosition}
    , msPerMove{msPerMove}
    , move{move}
    , line{line}
    , engine{EngineX::newEngine(engineIndex)}
{
    setObjectName("evaluationworker" + std::to_string(moveNumber));
    //std::cerr << "GameEvaluationWorker::GameEvaluationWorker " << moveNumber << " engine " << engine << "\n";
    if (!engine)
    {
        throw std::runtime_error{"Failed to instantiate engine"};
    }
    if (!engine->m_mapOptionValues.contains("Threads"))
    {
        throw std::runtime_error{"Could not set engine threads to 1."};
    }
    engine->m_mapOptionValues["Threads"] = 1;
    engine->setStartPos(startPosition);
    engine->setObjectName("engineforworker" + std::to_string(moveNumber));
    //std::cerr << "    " << moveNumber << " Connecting to engine " << engine << '\n';
    connect(engine, &EngineX::activated, this, &GameEvaluationWorker::engineActivated);
    connect(engine, &EngineX::deactivated, this, &GameEvaluationWorker::engineDeactivated);
    connect(engine, &EngineX::error, this, &GameEvaluationWorker::engineError);
    connect(engine, &EngineX::analysisStarted, this, &GameEvaluationWorker::engineAnalysisStarted);
    connect(engine, &EngineX::analysisStopped, this, &GameEvaluationWorker::engineAnalysisStopped);
    connect(engine, &EngineX::analysisUpdated, this, &GameEvaluationWorker::engineAnalysisUpdated);
    connect(engine, &EngineX::logUpdated, this, &GameEvaluationWorker::engineLogUpdated);
    engine->activate();
    running = true;
    //std::cerr << "    " << moveNumber << " activated engine\n";
}

GameEvaluationWorker::~GameEvaluationWorker() noexcept
{
    if (!engine)
    {
        return;
    }
    try
    {
        std::cerr << "~GameEvalutaionworker; " << moveNumber << " engine is " << engine << '\n';
        std::cerr << "    stopping analysis...\n";
        engine->deactivate();
        std::cerr << "    deleting engine\n";
        delete engine;
    }
    catch (...)
    {
        std::cerr << "    ~GameEvaluationWorker caught exception\n";
        // I don't think we can do anything about this exception.
    }
}

MoveId GameEvaluationWorker::getMove() const noexcept
{
    return move;
}

void GameEvaluationWorker::engineActivated()
{
    std::cerr << moveNumber << " EVENT: Engine activated; starting analysis...\n";
    EngineParameter parameters {msPerMove};
    engine->startAnalysis(currentPosition, 1, parameters, false, "");
}

void GameEvaluationWorker::engineDeactivated()
{
    std::cerr << moveNumber << " EVENT: engine deactivated\n";
}

void GameEvaluationWorker::engineError(QProcess::ProcessError)
{
    std::cerr << moveNumber << " EVENT: engine error\n";
}

void GameEvaluationWorker::engineAnalysisStarted()
{
    std::cerr << moveNumber << " EVENT:  analysis started\n";
    startTimestamp = QDateTime::currentDateTimeUtc();
}

void GameEvaluationWorker::engineAnalysisStopped()
{
    std::cerr << moveNumber << " EVENT: analysis stopped\n";
}

void GameEvaluationWorker::engineAnalysisUpdated(Analysis const & analysis)
{
    if (analysis.bestMove())
    {
        // When the engine reports a best move, no score is reported, so we skip it
        return;
    }
    if (analysis.isMate())
    {
        lastScore = static_cast<double>(10);
        if (std::signbit(analysis.score()))
            lastScore *= -1;
        // If it's black's turn and black is winning, analysis.score() returns a
        // positive number in a "mating" condition. We need a score from white's
        // perspective.
        bool blacksTurn {static_cast<bool>(analysis.variation().size() % 2)};
        if (!blacksTurn)
            lastScore *= -1;
    }
    else
    {
        lastScore = analysis.fscore();
    }
    if (analysis.getBookMove())
    {
        std::cerr << "BOOK MOVE!\n";
        engine->deactivate();
    }
    //lastScore = static_cast<double>(moveNumber % 2 ? -1 : 1);
    std::cerr << moveNumber << " EVENT: analysis updated. move " << move << " book move " << analysis.getBookMove() << " score "
        << lastScore << " ismate " << analysis.isMate() << " mate in " << analysis.movesToMate() << "\n";
    //std::cerr << moveNumber << " EVENT: analysis updated: " << analysis.toString(currentPosition).toStdString() << '\n';
}

void GameEvaluationWorker::engineLogUpdated()
{
    std::cerr << moveNumber << " EVENT: log updated\n";
}

double GameEvaluationWorker::getLastScore() const noexcept
{
    return lastScore;
}

bool GameEvaluationWorker::isRunning() const noexcept
{
    return running;
}

void GameEvaluationWorker::update() noexcept
{
    if (!startTimestamp)
    {
        // Maybe have a timeout and kill the engine if analysis still hasn't started?
        //std::cerr << moveNumber << " worker update called before starting, skipping...\n";
        return;
    }
    //std::cerr << moveNumber << " Worker update called, checking time... ms run " << startTimestamp->msecsTo(QDateTime::currentDateTimeUtc()) << "\n";
    if (startTimestamp->msecsTo(QDateTime::currentDateTimeUtc()) < msPerMove)
        return;
    engine->deactivate();
    running = false;
    // We'll let the destructor delete the engine since this is called during computation when
    // processing time is more premium than later.
}
