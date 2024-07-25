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
    std::cerr << "GameEvaluation::start... thread " << QThread::currentThread()->objectName().toStdString() << "\n";
    if (running)
    {
        throw std::logic_error{"Game evaluation already running"};
    }
    running = true;
    GameX gameCopy {game};
    gameCopy.moveToStart();
    workers.clear();
    workers.reserve(gameCopy.cursor().countMoves());
    QString line {""};
    unsigned count{0};
    do
    {
        try
        {
            workers.push_back(std::make_unique<GameEvaluationWorker>(engineIndex, gameCopy.startingBoard(),
                    gameCopy.board(), msPerMove, gameCopy.currentMove(), line, count));
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
        if (count++ > 10)
            break;
    } while(true);
    timer.stop();
    timer.start(std::chrono::milliseconds{100});
    connect(&timer, &QTimer::timeout, this, &GameEvaluation::update);
}

void GameEvaluation::update() noexcept
{
    std::unordered_map<MoveId, double> evaluations;
    for (unsigned i{0}; i < workers.size(); ++i)
    {
        GameEvaluationWorker_ptr & worker {workers[i]};
        worker->update();
        evaluations.emplace(worker->getMove(), worker->getLastScore());
        if (!worker->isRunning())
        {
            // Erase *after* getting the value because erasing will destroy the worker
            workers.erase(workers.begin()+i);
        }
    }
    std::cerr << "GameEvaluation::update... workers size " << workers.size() << " thread " << QThread::currentThread()->objectName().toStdString() << "\n";
    emit evaluationChanged(evaluations);
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
        int msPerMove, MoveId move, QString const & line, int num)
    : num{num}
    , startPosition{startPosition}
    , currentPosition{currentPosition}
    , msPerMove{msPerMove}
    , move{move}
    , line{line}
    , engine{EngineX::newEngine(engineIndex)}
{
    setObjectName("evaluationworker" + std::to_string(num));
    std::cerr << "GameEvaluationWorker::GameEvaluationWorker " << num << " engine " << engine << " thread " <<
        QThread::currentThread()->objectName().toStdString() << "\n";
    QThread::currentThread()->setObjectName("MAAIIIN" + std::to_string(num));
    std::cerr << "    changed thread name to " << QThread::currentThread()->objectName().toStdString() << "\n";
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
    engine->setObjectName("engineforworker" + std::to_string(num));
    std::cerr << "    " << num << " Connecting to engine " << engine << '\n';
    connect(engine, SIGNAL(activated()), this, SLOT(engineActivated()));
    connect(engine, SIGNAL(deactivated()), this, SLOT(engineDeactivated()));
    connect(engine, SIGNAL(error(QProcess::ProcessError)), this, SLOT(engineError(QProcess::ProcessError)));
    connect(engine, SIGNAL(analysisStarted()), this, SLOT(engineAnalysisStarted()));
    connect(engine, SIGNAL(analysisStopped()), this, SLOT(engineAnalysisStopped()));
    connect(engine, SIGNAL(analysisUpdated(Analysis const &)), this, SLOT(engineAnalysisUpdated(Analysis const&)));
    connect(engine, SIGNAL(logUpdated()), this, SLOT(engineLogUpdated()));
    engine->activate();
    running = true;
    std::cerr << "    " << num << " activated engine\n";
}

GameEvaluationWorker::~GameEvaluationWorker() noexcept
{
    if (!engine)
    {
        return;
    }
    try
    {
        std::cerr << "~GameEvalutaionworker; " << num << " engine is " << engine << '\n';
        std::cerr << "    stopping analysis...\n";
        engine->stopAnalysis();
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
    std::cerr << num << " EVENT: Engine activated; starting analysis... thread " << QThread::currentThread()->objectName().toStdString() << "\n";
    EngineParameter parameters {msPerMove};
    engine->startAnalysis(currentPosition, 1, parameters, false, "");
}

void GameEvaluationWorker::engineDeactivated()
{
    std::cerr << num << " EVENT: engine deactivated thread " << QThread::currentThread()->objectName().toStdString() << "\n";
}

void GameEvaluationWorker::engineError(QProcess::ProcessError)
{
    std::cerr << num << " EVENT: engine error thread " << QThread::currentThread()->objectName().toStdString() << "\n";
}

void GameEvaluationWorker::engineAnalysisStarted()
{
    std::cerr << num << " EVENT:  analysis started thread " << QThread::currentThread()->objectName().toStdString() << "\n";
    startTimestamp = QDateTime::currentDateTimeUtc();
}

void GameEvaluationWorker::engineAnalysisStopped()
{
    std::cerr << num << " EVENT: analysis stopped thread " << QThread::currentThread()->objectName().toStdString() << "\n";
}

void GameEvaluationWorker::engineAnalysisUpdated(Analysis const & analysis)
{
    if (analysis.bestMove())
    {
        // When the engine reports a best move, no score is reported, so we skip it
        return;
    }
    lastScore = analysis.fscore();
    std::cerr << num << " EVENT: analysis updated. move " << move << " score " << lastScore << " bestmove " <<
        analysis.bestMove() <<" depth " << analysis.depth() << "\n";
    if (lastScore == 0.0)
        std::cerr << "Zero!?\n";
}

void GameEvaluationWorker::engineLogUpdated()
{
    std::cerr << num << " EVENT: log updated thread " << QThread::currentThread()->objectName().toStdString() << "\n";
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
        std::cerr << num << " worker update called before starting, skipping...\n";
        return;
    }
    //std::cerr << num << " Worker update called, checking time... ms run " << startTimestamp->msecsTo(QDateTime::currentDateTimeUtc()) << " thread " << QThread::currentThread()->objectName().toStdString() << "\n";
    if (startTimestamp->msecsTo(QDateTime::currentDateTimeUtc()) < msPerMove)
        return;
    engine->deactivate();
    running = false;
    // We'll let the destructor delete the engine since this is called during computation when
    // processing time is more premium than later.
}
