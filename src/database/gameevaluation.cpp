#include <QFile>
#include <iostream>
#include "enginex.h"
#include "gamex.h"
#include "gameevaluation.h"

GameEvaluation::GameEvaluation(int engineIndex, int msPerMove, GameX game) noexcept
    : engineIndex{engineIndex}
    , msPerMove{msPerMove}
    , game{game}
{
}

void GameEvaluation::start()
{
    std::cout << "GameEvaluation::start...\n";
    if (running)
    {
        throw std::logic_error{"Game evaluation already running"};
    }
    running = true;
    GameX gameCopy {game};
    gameCopy.moveToStart();
    workers.clear();
    QString line {""};
    do
    {
        try
        {
            workers.emplace_back(engineIndex, gameCopy.startingBoard(), gameCopy.board(), msPerMove, gameCopy.currentMove(), line);
        }
        catch (...)
        {
            // Destroy any workers that have started.
            workers.clear();
            throw std::runtime_error{"Failed to start all evaluation workers"};
        }
        if (!gameCopy.forward())
        {
            break;
        }
        // See gamecursor.cpp::moveToId(MoveId, QString*)
        line.push_back(gameCopy.move().toAlgebraic());
        line.push_back(" ");
        break;
    } while(true);
    timer.stop();
    timer.start(std::chrono::milliseconds{100});
    connect(&timer, &QTimer::timeout, this, &GameEvaluation::update);
}

void GameEvaluation::update() noexcept
{
    std::cout << "GameEvaluation::updated...\n";
    std::unordered_map<MoveId, double> evaluations;
    for (unsigned i{0}; i < workers.size(); ++i)
    {
        GameEvaluationWorker & worker = workers[i];
        evaluations.emplace(worker.getMove(), worker.getLastScore());
        if (!worker.isRunning())
        {
            // Erase *after* getting the value because erasing will destroy the worker
            workers.erase(workers.begin()+i);
        }
    }
    emit evaluationChanged(evaluations);
    if (!workers.size())
    {
        timer.stop();
        running=false;
    }
}

void GameEvaluation::stop() noexcept
{
    workers.clear();
    timer.stop();
    running = false;
}

GameEvaluation::~GameEvaluation() noexcept
{
    stop();
}

GameEvaluationWorker::GameEvaluationWorker(int engineIndex, BoardX const & startPosition, BoardX const & currentPosition,
        int msPerMove, MoveId move, QString const & line)
    : startPosition{startPosition}
    , currentPosition{currentPosition}
    , msPerMove{msPerMove}
    , move{move}
    , line{line}
    , engine{EngineX::newEngine(engineIndex)}
{
    std::cout << "GameEvaluationWorker::GameEvaluationWorker\n";
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
    connect(engine, &EngineX::activated, this, &GameEvaluationWorker::engineActivated);
    connect(engine, &EngineX::deactivated, this, &GameEvaluationWorker::engineDeactivated);
    connect(engine, &EngineX::error, this, &GameEvaluationWorker::engineError);
    connect(engine, &EngineX::analysisStarted, this, &GameEvaluationWorker::engineAnalysisStarted);
    connect(engine, &EngineX::analysisStopped, this, &GameEvaluationWorker::engineAnalysisStopped);
    connect(engine, &EngineX::analysisUpdated, this, &GameEvaluationWorker::engineAnalysisUpdated);
    connect(engine, &EngineX::logUpdated, this, &GameEvaluationWorker::engineLogUpdated);
    engine->activate();
    running = true;
    std::cout << "    activated engine\n";
}

GameEvaluationWorker & GameEvaluationWorker::operator=(GameEvaluationWorker && other) noexcept
{
    this->move = other.move;
    std::swap(this->line, other.line);
    return *this;
}

GameEvaluationWorker::~GameEvaluationWorker() noexcept
{
    std::cout << "~GameEvalutaionworker; engine is " << engine << '\n';
    if (!engine)
    {
        return;
    }
    try
    {
        std::cout << "    stopping analysis...\n";
        engine->stopAnalysis();
        std::cout << "    deleting engine\n";
        delete engine;
    }
    catch (...)
    {
        std::cout << "    ~GameEvaluationWorker caught exception\n";
        // I don't think we can do anything about this exception.
    }
}

MoveId GameEvaluationWorker::getMove() const noexcept
{
    return move;
}

GameEvaluationWorker::GameEvaluationWorker(GameEvaluationWorker && other) noexcept
    : startPosition{std::move(other.startPosition)}
    , currentPosition{std::move(other.currentPosition)}
    , msPerMove{other.msPerMove}
    , move{other.move}
    , line{std::move(other.line)}
    , engine{other.engine}
{
    std::cout << "GameEvaluationWorker copy cctor \n";
    other.engine = nullptr;
}

void GameEvaluationWorker::engineActivated()
{
    std::cout << "Engine activated event received; starting analysis...\n";
    EngineParameter parameters {msPerMove};
    engine->startAnalysis(currentPosition, 1, parameters, false, "");
}

void GameEvaluationWorker::engineDeactivated()
{
    std::cout << "engine deactivated event received\n";
}

void GameEvaluationWorker::engineError(QProcess::ProcessError)
{
    std::cout << "engine error event received\n";
}

void GameEvaluationWorker::engineAnalysisStarted()
{
    std::cout << " analysis started event received\n";
}

void GameEvaluationWorker::engineAnalysisStopped()
{
    std::cout << "analysis stopped event received\n";
}

void GameEvaluationWorker::engineAnalysisUpdated(Analysis const & analysis)
{
    lastScore = analysis.fscore();
    std::cout << "analysis updated. event received\n";
}

void GameEvaluationWorker::engineLogUpdated()
{
    std::cout << "log updated event received\n";
}

double GameEvaluationWorker::getLastScore() const noexcept
{
    return lastScore;
}

bool GameEvaluationWorker::isRunning() const noexcept
{
    return running;
}
