#define TRAINING_TEST 1
#include "databaseinfo.h"
#include "training.h"
#include "database.h"
#include "output.h"
#include <QDateTime>
#include <iostream>

static constexpr char const * datetime_format = "yyyy-MM-ddThh:mm:ss";

static const QRegularExpression last_reviewed_regex("last reviewed: ([^;]+);");
static std::time_t get_last_reviewed_from_annotation(QString const & annotation)
{
    QRegularExpressionMatch match = last_reviewed_regex.match(annotation);
    if (match.hasMatch())
    {
        QDateTime dt = QDateTime::fromString(match.captured(1), datetime_format);
        return dt.currentSecsSinceEpoch();
    }
    return 0;
}

static const QRegularExpression first_reviewed_regex("first reviewed: ([^;]+);");
static std::time_t get_first_reviewed_from_annotation(QString const & annotation)
{
    QRegularExpressionMatch match = first_reviewed_regex.match(annotation);
    if (match.hasMatch())
    {
        QDateTime dt = QDateTime::fromString(match.captured(1), datetime_format);
        return dt.currentSecsSinceEpoch();
    }
    return 0;
}

static const QRegularExpression next_review_regex("next review: ([^;]+);");
static std::time_t get_next_review_from_annotation(QString const & annotation)
{
    QRegularExpressionMatch match = next_review_regex.match(annotation);
    if (match.hasMatch())
    {
        QDateTime dt = QDateTime::fromString(match.captured(1), datetime_format);
        return dt.currentSecsSinceEpoch();
    }
    return 0;
}

// Sort training_lines
static bool operator<(training_line const & lhs, training_line const & rhs)
{
    if (!lhs.has_been_seen || !rhs.has_been_seen)
    {
        if (!lhs.has_been_seen)
        {
            if (!rhs.has_been_seen)
            {
                // For unseen lines, look at the longest one first.
                // This may be a mistake, but I would think we want to learn
                // the "main line" first.
                return lhs.moves.size() < rhs.moves.size();
            }
            // Return a line that has been seen above a new one
            return true;
        }
        return false;
    }
    return lhs.next_review < rhs.next_review;
}

static training_line get_training_line(GameX & game, GameId id)
{
    std::vector<Move> moves;
    moves.reserve(game.plyCount());
    MoveId current_spot {game.cursor().currMove()};
    while (current_spot != NO_MOVE && current_spot != ROOT_NODE)
    {
        moves.push_back(game.move(current_spot));
        current_spot = game.cursor().prevMove(current_spot);
    }
    std::reverse(moves.begin(), moves.end());
    QString annotation = game.annotation();
    bool has_been_seen = annotation.length();
    std::time_t last_reviewed = get_last_reviewed_from_annotation(annotation);
    std::time_t first_reviewed = get_first_reviewed_from_annotation(annotation);
    std::time_t next_review = get_next_review_from_annotation(annotation);
    return {std::move(moves), has_been_seen, first_reviewed, last_reviewed, next_review, game.currentMove(), &game, id};
}

/** Reads all child nodes in the game and adds a training_line for
 * any leaf nodes found
 */
void get_training_lines(GameX & game, GameId id, std::vector<training_line> & lines)
{
    if (game.nextMove() == NO_MOVE)
    {
        lines.emplace_back(get_training_line(game, id));
        return;
    }
    game.forward();
    get_training_lines(game, id, lines);
    game.backward();
    if (!game.variationCount())
        return;
    for (MoveId const & variation_move : game.variations())
    {
        game.enterVariation(variation_move);
        get_training_lines(game, id, lines);
        game.backward();
    }
}

void Training::initialize(Database & db, Color color)
{
    for (quint64 game_id{0}; game_id < db.count(); ++game_id)
    {
        games.emplace_back();
        if (!db.loadGame(game_id, games.back()))
        {
            std::cerr << "Failed to load game " << game_id << " continuing...\n";
            continue;
        }
        games.back().moveToStart();
        std::cout << "     initialized with game " << game_id << " has " << games.back().cursor().countMoves() << " moves.\n";
        get_training_lines(games.back(), game_id, lines);
    }
    std::sort(lines.begin(), lines.end());
    // If the trainee is White, it's his turn.
    // If the trainee is Black, the trainer has already made a move for white.
    current_move_in_line = color == White ? 0 : 1;
    this->training_color = color;
}

bool Training::move(Move const & new_move)
{
    if (lines.empty())
    {
        std::cerr << "In Training::move, no training lines\n";
        return false;
    }
    try
    {
        Move const & expected_move {lines.front().moves.at(current_move_in_line)};
        // bool operator==(Move const &, Move const &) may be a little too picky for us.
        if (expected_move.to() != new_move.to() || expected_move.from() != new_move.from())
        {
            missed_any_this_time = true;
            return false;
        }
        // Increment the current move marker for the trainee and trainer.
        current_move_in_line += 2;
        handle_done();
        return true;
    }
    catch (std::out_of_range const &)
    {
        return false;
    }
    return true;
}

Move Training::last_response(void) const
{
    if (!current_move_in_line)
        // If trainee is white, there was no last response
        // If the trainee is block, the "current move" should never be 0
        return {chessx::Square::InvalidSquare, chessx::Square::InvalidSquare};
    if (lines.empty())
    {
        std::cerr << "In Training::last_response, no training lines\n";
        return {chessx::Square::InvalidSquare, chessx::Square::InvalidSquare};
    }
    try
    {
        return lines.front().moves.at(current_move_in_line-1);
    }
    catch (std::out_of_range const &)
    {
        return {chessx::Square::InvalidSquare, chessx::Square::InvalidSquare};
    }
}

std::optional<GameId> Training::get_game_id(void) const
{
    if (lines.empty())
    {
        std::cerr << "In Training::get_game_id, no training lines\n";
        return 0;
    }
    return lines.front().game_id;
}
GameX * Training::get_game(void)
{
    if (lines.empty())
    {
        std::cerr << "In Training::get_game, no training lines\n";
        return nullptr;
    }
    return lines.front().game;
}

// Returns true if we're done training this line, handles bookkeeping
// to record training progress on the current line, and resets the training object.
bool Training::handle_done()
{
    if (!finished_current_training() || lines.empty())
    {
        return false;
    }
    training_line & line {lines.front()};
    if (!line.game)
    {
        std::cerr << "in Training::handle_done(), training line doesn't have a game reference.\n";
        return false;
    }
    line.last_reviewed = std::time(nullptr);
    int increment_sign {missed_any_this_time ? -1 : 1};
    std::time_t increment {line.next_review - line.last_reviewed};
    increment *= increment_sign;
    std::time(&line.last_reviewed);
    line.next_review += increment;
    QDateTime last_reviewed = QDateTime::fromSecsSinceEpoch(line.last_reviewed);
    QDateTime next_review = QDateTime::fromSecsSinceEpoch(line.last_reviewed);
    QString new_annotation = QString{"last reviewed: %1; next review: %2"}
        .arg(last_reviewed.toString(datetime_format)).arg(next_review.toString(datetime_format));
    line.game->dbSetAnnotation(new_annotation, line.leaf_id, GameX::Position::AfterMove);
    return true;
}

bool Training::finished_current_training(void) const
{
    if (lines.empty())
    {
        std::cerr << "In Training::finished_current_training, no training lines\n";
        return false;
    }
    return current_move_in_line >= lines.front().moves.size();
}

bool Training::done_training(void) const
{
    std::time_t const midnight_this_morning{QDateTime{QDate::currentDate(), {}}.currentSecsSinceEpoch()};
    unsigned new_lines_studied_today {0};
    bool new_lines_to_study {false};
    for (training_line const & line : lines)
    {
        if (line.last_reviewed != 0)
        {
            continue;
        }
        if (line.first_reviewed != 0 && line.first_reviewed >= midnight_this_morning)
        {
            ++new_lines_studied_today;
        }
        new_lines_to_study = true;
    }
    if (new_lines_to_study && new_lines_studied_today < this->new_lines_per_day)
    {
        return false;
    }
    std::time_t const now{std::chrono::system_clock::to_time_t(std::chrono::system_clock::now())};
    for (training_line const & line : lines)
    {
        if (line.next_review <= now)
        {
            return false;
        }
    }
    return true;
}

Training::Training(unsigned n)
    : new_lines_per_day{n}
{
}

#if TRAINING_TEST
#include "memorydatabase.h"
#include "settings.h"
#include "chessxsettings.h"

void test_successful_review(Database & db)
{
    std::vector<Move> moves{
        {chessx::Square::e2, chessx::Square::e4},
        {chessx::Square::g1, chessx::Square::f3},
        {chessx::Square::f1, chessx::Square::b5},
    };
    Training t {2};
    std::cout << "test_successful_review...\n";
    t.initialize(db, White);
    for (Move m : moves)
    {
        // Check for variation
        if (t.last_response().from() == chessx::Square::d7)
        {
            m = {chessx::Square::b1, chessx::Square::c3};
        }
        if (!t.move(m))
        {
            std::cerr << "   -Failure on  move " << m.toAlgebraicDebug().toStdString() << "\n";
            break;
        }
    }
    if (!t.finished_current_training())
    {
        std::cerr << "   Training should be finished_current_training\n";
        return;
    }
    GameX * updated_game = t.get_game();
    std::optional<GameId> updated_game_id = t.get_game_id();
    if (!updated_game || !updated_game_id)
    {
        std::cerr << "   Couldn't get trained game\n";
        return;
    }
    db.replace(*updated_game_id, *updated_game);
    Output output(Output::Pgn);
    // Cannot output to the same file that the DB is currently using.
    output.outputLatin1("../test-successful-result.pgn", db);
    std::cout << "test complete\n";
}

void test_failed_review(Database & db)
{
    std::vector<Move> moves{
        {chessx::Square::e2, chessx::Square::e4},
        {chessx::Square::g1, chessx::Square::f3},
        {chessx::Square::f1, chessx::Square::b5},
    };
    Move bad_move{chessx::Square::e1, chessx::Square::d1};
    Training t;
    std::cout << "test_failed_review...\n";
    t.initialize(db, White);
    for (Move m : moves)
    {
        if (t.move(bad_move))
        {
            std::cerr << "   Bad move should be rejected\n";
        }
        if (t.finished_current_training())
        {
            std::cerr << "   Training should not be over after bad move\n";
        }
        // Check for variation
        if (t.last_response().from() == chessx::Square::d7)
        {
            m = {chessx::Square::b1, chessx::Square::c3};
        }
        if (!t.move(m))
        {
            std::cerr << "   -Failure on  move " << m.toAlgebraicDebug().toStdString() << "\n";
            break;
        }
    }
    if (!t.finished_current_training())
    {
        std::cerr << "   Training should be finished\n";
        return;
    }
    GameX * updated_game = t.get_game();
    std::optional<GameId> updated_game_id = t.get_game_id();
    if (!updated_game || !updated_game_id)
    {
        std::cerr << "   Couldn't get trained game\n";
        return;
    }
    db.replace(*updated_game_id, *updated_game);
    Output output(Output::Pgn);
    // Cannot output to the same file that the DB is currently using.
    output.outputLatin1("../test-failed-result.pgn", db);
    std::cout << "test complete\n";
}

void train_one_per_day(Database & db)
{
    std::vector<Move> moves{
        {chessx::Square::e2, chessx::Square::e4},
        {chessx::Square::g1, chessx::Square::f3},
        {chessx::Square::f1, chessx::Square::b5},
    };
    Move bad_move{chessx::Square::e1, chessx::Square::d1};
    Training t {1};
    std::cout << "train_one_per_day...\n";
    t.initialize(db, White);
    for (Move m : moves)
    {
        // Check for variation
        if (t.last_response().from() == chessx::Square::d7)
        {
            m = {chessx::Square::b1, chessx::Square::c3};
        }
        if (!t.move(m))
        {
            std::cerr << "   -Failure on  move " << m.toAlgebraicDebug().toStdString() << "\n";
            break;
        }
    }
    if (!t.finished_current_training())
    {
        std::cerr << "   Training should be finished\n";
        return;
    }
    if (!t.done_training())
    {
        std::cerr << "   Training should be done today\n";
        return;
    }
    std::cout << "test complete\n";
}

void test_training(void)
{
    // memoryDatabase needs this global
    AppSettings = new ChessXSettings;
    AppSettings->setValue("/General/automaticECO", false);
    char const * const db_file{"../practice.pgn"};
    std::vector<std::function<void(Database &)>> tests {test_successful_review, test_failed_review,
        train_one_per_day};
    for (std::function<void(Database &)> const & test : tests)
    {
        MemoryDatabase db;
        if (!db.open(db_file, true))
        {
            std::cerr << "Failed to open test training file\n";
            return;
        }
        if (!static_cast<Database&>(db).parseFile())
        {
            std::cerr << "Failed to parse test training file\n";
            return;
        }
        test(db);
    }
}
#endif
