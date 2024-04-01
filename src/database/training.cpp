#define TRAINING_TEST 1
#include "databaseinfo.h"
#include "training.h"
#include "database.h"
#include "output.h"
#include <QDateTime>
#include <iostream>
#include <thread>

static constexpr char const * datetime_format = "yyyy-MM-ddThh:mm:ss";

static const QRegularExpression last_reviewed_regex("last reviewed: ([^;]+);", QRegularExpression::CaseInsensitiveOption);
static std::time_t get_last_reviewed_from_annotation(QString const & annotation)
{
    QRegularExpressionMatch match = last_reviewed_regex.match(annotation);
    if (match.hasMatch())
    {
        QDateTime dt = QDateTime::fromString(match.captured(1), datetime_format);
        return dt.toSecsSinceEpoch();
    }
    return 0;
}

static const QRegularExpression first_reviewed_regex("first reviewed: ([^;]+);", QRegularExpression::CaseInsensitiveOption);
static std::time_t get_first_reviewed_from_annotation(QString const & annotation)
{
    QRegularExpressionMatch match = first_reviewed_regex.match(annotation);
    if (match.hasMatch())
    {
        QDateTime dt = QDateTime::fromString(match.captured(1), datetime_format);
        return dt.toSecsSinceEpoch();
    }
    return 0;
}

static const QRegularExpression next_review_regex("next review: ([^;]+);", QRegularExpression::CaseInsensitiveOption);
static std::time_t get_next_review_from_annotation(QString const & annotation)
{
    QRegularExpressionMatch match = next_review_regex.match(annotation);
    if (match.hasMatch())
    {
        //std::cout << "     next review parse, got one! " << match.captured(1).toStdString() << '\n';
        QDateTime dt = QDateTime::fromString(match.captured(1), datetime_format);
        return dt.toSecsSinceEpoch();
    }
    return 0;
}

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
                return lhs.moves.size() > rhs.moves.size();
            }
            // Return a line that has been seen above a new one
            return false;
        }
        return true;
    }
    return lhs.next_review > rhs.next_review;
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
    //std::cout << "   parsed training line annotation: \"" << annotation.toStdString() << "\"\n";
    //std::cout << "     last review " << last_reviewed << '\n';
    //std::cout << "     next review " << next_review << '\n';
    //std::cout << "     first reviewed " << first_reviewed << '\n';
    return {std::move(moves), has_been_seen, first_reviewed, last_reviewed, next_review, game.currentMove(), &game, id};
}

/** Reads all child nodes in the game and adds a training_line for
 * any leaf nodes found
 */
static void get_training_lines(GameX & game, GameId id, std::vector<training_line> & lines)
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

bool Training::missed_any(void) const
{
    return missed_any_this_time;
}

bool Training::initialize(Database & db, Color color)
{
    lines.clear();
    for (quint64 game_id{0}; game_id < db.count(); ++game_id)
    {
        games.emplace_back();
        if (!db.loadGame(game_id, games.back()))
        {
            std::cerr << "Failed to load game " << game_id << " continuing...\n";
            continue;
        }
        games.back().moveToStart();
        get_training_lines(games.back(), game_id, lines);
    }
    if (!lines.size())
    {
        return false;
    }
    std::sort(lines.begin(), lines.end());
    std::time_t const midnight_this_morning{QDateTime{QDate::currentDate(), {}}.toSecsSinceEpoch()};
    auto const new_lines_learned_today {std::count_if(lines.begin(), lines.end(),
                [&midnight_this_morning] (training_line const & val) {return val.first_reviewed >= midnight_this_morning;})};
    if (lines.size() > 1 && lines.front().has_been_seen && !lines.back().has_been_seen && new_lines_learned_today < this->new_lines_per_day)
    {
        // We have new lines to learn and we haven't learned the limit for today,
        // so put one of the new ones on top to learn.
        std::swap(lines.front(), lines.back());
    }
    // If the trainee is White, it's his turn.
    // If the trainee is Black, the trainer has already made a move for white.
    current_move_in_line = color == White ? 0 : 1;
    missed_any_this_time = false;
    this->training_color = color;
    return true;
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
    // If the trainee missed any, let's reduce the time to review again
    int const increment_sign {missed_any_this_time ? -1 : 1};
    std::time_t increment {(line.next_review - line.last_reviewed)};
    //std::cout << "  ::handle_done. old next review " << line.next_review << " old last reviewed " << line.last_reviewed << 
        //" increment " << increment << '\n';
    if (increment == 0)
    {
        //std::cout << "   using initial increment of " << initial_increment << '\n';
        // Start with 20 second review period
        increment = initial_increment;
    }
    increment *= increment_sign;
    std::time(&line.last_reviewed);
    //std::cout << "    setting last reviewed to " << line.last_reviewed << '\n';
    if (!line.first_reviewed)
    {
        //std::cout << "     First review!\n";
        line.first_reviewed = line.last_reviewed;
    }
    QDateTime const first_reviewed = QDateTime::fromSecsSinceEpoch(line.first_reviewed);
    line.next_review = line.last_reviewed + increment;
    //std::cout << "     setting next review to " << line.next_review << '\n';
    QDateTime const last_reviewed = QDateTime::fromSecsSinceEpoch(line.last_reviewed);
    QDateTime const next_review = QDateTime::fromSecsSinceEpoch(line.next_review);
    QString new_annotation = QString{"first reviewed: %1; last reviewed: %2; next review: %3"}
        .arg(first_reviewed.toString(datetime_format))
        .arg(last_reviewed.toString(datetime_format))
        .arg(next_review.toString(datetime_format));
    //std::cout << "    ::handle_done, addingannotation " << new_annotation.toStdString() << '\n';
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

bool Training::done_training_today(void) const
{
    std::time_t const midnight_this_morning{QDateTime{QDate::currentDate(), {}}.toSecsSinceEpoch()};
    unsigned new_lines_studied_today {0};
    bool new_lines_to_study {false};
    for (training_line const & line : lines)
    {
        if (line.last_reviewed == 0)
        {
            new_lines_to_study = true;
        }
        else if (line.first_reviewed >= midnight_this_morning)
        {
            ++new_lines_studied_today;
        }
    }
    if (new_lines_to_study && new_lines_studied_today < this->new_lines_per_day)
    {
        return false;
    }
    std::time_t const now{std::chrono::system_clock::to_time_t(std::chrono::system_clock::now())};
    for (training_line const & line : lines)
    {
        if (line.next_review && line.next_review <= now)
        {
            return false;
        }
    }
    return true;
}

Training::Training(unsigned n, std::time_t i)
    : new_lines_per_day{n}, initial_increment{i}
{
}

#if TRAINING_TEST
#include "memorydatabase.h"
#include "settings.h"
#include "chessxsettings.h"

static std::string to_string(training_line const & l)
{
    std::ostringstream str;
    str << "line {";
    for (Move const & m : l.moves)
    {
        str << m.toAlgebraicDebug().toStdString() << ',';
    }
    str << "; seen? " << l.has_been_seen;
    str << "; next rev " << l.next_review;
    str << "}";
    return str.str();
}

std::vector<training_line> & Training::get_lines(void)
{
    return lines;
}

static const QString starting_game {R"(1. e4 e5 2. Nf3 Nc6 ( d5 3. Nc3 ) 3. Bb5 h6 4. h3 *)"};
static const QString starting_game_one {R"(1. e4 e5 2. Nf3 Nc6
    ( d5 3. Nc3{first reviewed: 2023-01-01T00:00:00; last reviewed: 2024-01-01T00:00:00; next review: 2024-01-02T00:00:00;} )
    3. Bb5     {first reviewed: 2023-01-01T00:00:00; last reviewed: 2024-01-01T00:00:00; next review: 2024-01-02T00:00:01;} *)"};
static const QString starting_game_two {R"(1. e4 e5 2. Nf3 Nc6 ( d5 3. Nc3{first reviewed: 2023-01-01T00:00:00; last reviewed: 2024-01-01T00:00:00; next review: 2024-01-02T00:00:00;}) 3. Bb5 Nf6 4. Nc3 {first reviewed: 2023-01-01T00:00:00; last reviewed: 2024-01-01T00:00:00; next review: 2024-01-03T00:00:01;} *)"};
static const QString starting_game_three {R"(1. e4 e5 2. Nf3 Nc6
    ( d5 3. Nc3 )
    3. Bb5     {first reviewed: 2023-01-01T00:00:00; last reviewed: 2024-01-01T00:00:00; next review: 2024-01-02T00:00:01;} *)"};

// moves is the moves for both sides
static bool play_through_line(Training & t, std::vector<Move> const & moves)
{
    unsigned move_number{0};
    //std::cout << " playing through line...\n";
    for (Move m : moves)
    {
        //std::cout << "   move num " << move_number << " working on " << m.toAlgebraicDebug().toStdString() << '\n';
        if (move_number % 2)
        { // odd move
            Move const found_move {t.last_response()};
            if (found_move.from() != m.from() || found_move.to() != m.to())
            {
                std::cerr << "   Move " << move_number << " was not the expected " << m.toAlgebraicDebug().toStdString()
                    << " but " << t.last_response().toAlgebraicDebug().toStdString() << ".\n";
                return false;
            }
        }
        else if (!t.move(m))
        {
            std::cerr << "   -Failure on  move " << move_number << " (attempted " << m.toAlgebraicDebug().toStdString() << ")\n";
            return false;
        }
        ++move_number;
    }
    if (!t.finished_current_training())
    {
        std::cerr << "   Training should be finished\n";
        return false;
    }
    return true;
}

static bool successful_review(MemoryDatabase & db)
{
    // Since we're dealing with two new lines, the longer line should be chosen
    std::vector<Move> moves{
        {chessx::Square::e2, chessx::Square::e4},
        {chessx::Square::g1, chessx::Square::f3},
        {chessx::Square::f1, chessx::Square::b5},
        {chessx::Square::h2, chessx::Square::h3},
    };
    Training t {2};
    t.initialize(db, White);
    for (Move m : moves)
    {
        if (!t.move(m))
        {
            std::cerr << "   -Failure on  move " << m.toAlgebraicDebug().toStdString() << "\n";
            break;
        }
    }
    if (!t.finished_current_training())
    {
        std::cerr << "   Training should be finished_current_training\n";
        return false;
    }
    GameX * updated_game = t.get_game();
    std::optional<GameId> updated_game_id = t.get_game_id();
    if (!updated_game || !updated_game_id)
    {
        std::cerr << "   Couldn't get trained game\n";
        return false;
    }
    db.replace(*updated_game_id, *updated_game);
    Output output(Output::Pgn);
    // Cannot output to the same file that the DB is currently using.
    output.outputLatin1("../test-successful-result.pgn", db);
    return true;
}

static bool failed_review(MemoryDatabase & db)
{
    std::vector<Move> moves{
        {chessx::Square::e2, chessx::Square::e4},
        {chessx::Square::g1, chessx::Square::f3},
        {chessx::Square::f1, chessx::Square::b5},
        {chessx::Square::h2, chessx::Square::h3},
    };
    Move bad_move{chessx::Square::e1, chessx::Square::d1};
    Training t;
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
        return false;
    }
    GameX * updated_game = t.get_game();
    std::optional<GameId> updated_game_id = t.get_game_id();
    if (!updated_game || !updated_game_id)
    {
        std::cerr << "   Couldn't get trained game\n";
        return false;
    }
    db.replace(*updated_game_id, *updated_game);
    Output output(Output::Pgn);
    // Cannot output to the same file that the DB is currently using.
    output.outputLatin1("../test-failed-result.pgn", db);
    return true;
}

static bool one_per_day(MemoryDatabase & db)
{
    std::vector<Move> moves{
        {chessx::Square::e2, chessx::Square::e4},
        {chessx::Square::g1, chessx::Square::f3},
        {chessx::Square::f1, chessx::Square::b5},
        {chessx::Square::h2, chessx::Square::h3},
    };
    Move bad_move{chessx::Square::e1, chessx::Square::d1};
    Training t {1};
    t.initialize(db, White);
    for (Move m : moves)
    {
        if (!t.move(m))
        {
            std::cerr << "   -Failure on  move " << m.toAlgebraicDebug().toStdString() << "\n";
            break;
        }
    }
    if (!t.finished_current_training())
    {
        std::cerr << "   Training should be finished\n";
        return false;
    }
    if (!t.done_training_today())
    {
        std::cerr << "   Training should be done today\n";
        return false;
    }
    return true;
}

static bool progress_to_next_training(MemoryDatabase & db)
{
    std::vector<Move> moves{{chessx::Square::e2, chessx::Square::e4},
                            {chessx::Square::e7, chessx::Square::e5},
                            {chessx::Square::g1, chessx::Square::f3},
                            {chessx::Square::b8, chessx::Square::c6},
                            {chessx::Square::f1, chessx::Square::b5},
    };
    Training t {2, 1};
    t.initialize(db, White);
    if (!play_through_line(t, moves))
    {
        std::cerr << "   Failed to play through first variation due\n";
        return false;
    }
    if (t.done_training_today())
    {
        std::cerr << "   Today's training should not be done yet\n";
        return false;
    }
    GameX * game{t.get_game()};
    if (!game)
    {
        std::cerr << "   Couldn't get training game\n";
        return false;
    }
    std::optional<GameId> game_id {t.get_game_id()};
    if (!game_id)
    {
        std::cerr << "   Couldn't get game id\n";
        return false;
    }
    db.replace(*game_id, *game);
    //std::cout << "   finished with first line (nc6); looking for d5...\n";
    t.initialize(db, White);
    moves = {{chessx::Square::e2, chessx::Square::e4},
             {chessx::Square::e7, chessx::Square::e5},
             {chessx::Square::g1, chessx::Square::f3},
             {chessx::Square::d7, chessx::Square::d5},
             {chessx::Square::b1, chessx::Square::c3},
    };
    if (!play_through_line(t, moves))
    {
        std::cerr << "   Failed to progress to next variation?\n";
        return false;
    }
    return true;
}

static bool sort_training_lines_oldest_to_review(MemoryDatabase &)
{
    std::vector<training_line> lines;
    training_line line0;
    line0.moves = {
        {chessx::Square::e2, chessx::Square::e4},
        {chessx::Square::e7, chessx::Square::e5},
        {chessx::Square::g1, chessx::Square::f3},
        {chessx::Square::b8, chessx::Square::c6},
        {chessx::Square::b1, chessx::Square::c3},
    };
    line0.has_been_seen = true;
    line0.first_reviewed = 1;
    line0.last_reviewed = 1;
    line0.next_review = 2;
    lines.push_back(std::move(line0));
    training_line line1;
    line1.moves = {
        {chessx::Square::e2, chessx::Square::e4},
        {chessx::Square::e7, chessx::Square::e5},
        {chessx::Square::g1, chessx::Square::f3},
        {chessx::Square::c7, chessx::Square::c6},
        {chessx::Square::c1, chessx::Square::c3},
    };
    line1.has_been_seen = true;
    line1.first_reviewed = 1;
    line1.last_reviewed = 1;
    line1.next_review = 3;
    lines.push_back(std::move(line1));
    std::sort(lines.begin(), lines.end());
    if (lines.at(0).moves.at(3).from() != chessx::Square::c7)
    {
        std::cerr << "    The first line should be the one that needed to be reviewed longest ago\n";
        return false;
    }
    return true;
}

static bool sort_training_lines_has_been_seen_first(MemoryDatabase &)
{
    std::vector<training_line> lines;
    training_line line0;
    line0.moves = {
        {chessx::Square::e2, chessx::Square::e4},
        {chessx::Square::e7, chessx::Square::e5},
        {chessx::Square::g1, chessx::Square::f3},
        {chessx::Square::b8, chessx::Square::c6},
        {chessx::Square::b1, chessx::Square::c3},
    };
    line0.has_been_seen = false;
    line0.first_reviewed = 0;
    line0.last_reviewed = 0;
    line0.next_review = 0;
    lines.push_back(std::move(line0));
    training_line line1;
    line1.moves = {
        {chessx::Square::e2, chessx::Square::e4},
        {chessx::Square::e7, chessx::Square::e5},
        {chessx::Square::g1, chessx::Square::f3},
        {chessx::Square::c7, chessx::Square::c6},
        {chessx::Square::c1, chessx::Square::c3},
    };
    line1.has_been_seen = true;
    line1.first_reviewed = 1;
    line1.last_reviewed = 1;
    line1.next_review = 3;
    lines.push_back(std::move(line1));
    std::sort(lines.begin(), lines.end());
    if (lines.at(0).moves.at(3).from() != chessx::Square::c7)
    {
        std::cerr << "    The line that has been seen should be reviewed before that which hasn't\n";
        return false;
    }
    return true;
}

static bool sort_training_lines_longest_first(MemoryDatabase &)
{
    std::vector<training_line> lines;
    training_line line0;
    line0.moves = {
        {chessx::Square::e2, chessx::Square::e4},
        {chessx::Square::e7, chessx::Square::e5},
        {chessx::Square::g1, chessx::Square::f3},
    };
    line0.has_been_seen = false;
    line0.first_reviewed = 0;
    line0.last_reviewed = 0;
    line0.next_review = 0;
    lines.push_back(std::move(line0));
    training_line line1;
    line1.moves = {
        {chessx::Square::e2, chessx::Square::e4},
        {chessx::Square::e7, chessx::Square::e5},
        {chessx::Square::g1, chessx::Square::f3},
        {chessx::Square::c7, chessx::Square::c6},
        {chessx::Square::c1, chessx::Square::c3},
    };
    line1.has_been_seen = false;
    line1.first_reviewed = 0;
    line1.last_reviewed = 0;
    line1.next_review = 0;
    lines.push_back(std::move(line1));
    std::sort(lines.begin(), lines.end());
    if (lines.at(0).moves.size() != 5)
    {
        std::cerr << "    Given two new lines, the longer should be learned first\n";
        return false;
    }
    return true;
}

static bool selects_new_lines(MemoryDatabase & db)
{
    std::vector<Move> moves = {{chessx::Square::e2, chessx::Square::e4},
             {chessx::Square::e7, chessx::Square::e5},
             {chessx::Square::g1, chessx::Square::f3},
             {chessx::Square::d7, chessx::Square::d5},
             {chessx::Square::b1, chessx::Square::c3},
    };
    Training t {1, 1};
    // Should initialize to the new game first
    t.initialize(db, White);
    if (!play_through_line(t, moves))
    {
        std::cerr << "   Failed to play through new game first variation due\n";
        return false;
    }
    if (t.done_training_today())
    {
        std::cerr << "   Today's training should not be done yet\n";
        return false;
    }
    GameX * game{t.get_game()};
    if (!game)
    {
        std::cerr << "   Couldn't get training game\n";
        return false;
    }
    std::optional<GameId> game_id {t.get_game_id()};
    if (!game_id)
    {
        std::cerr << "   Couldn't get game id\n";
        return false;
    }
    db.replace(*game_id, *game);
    //std::cout << "   finished with first line (nc6); looking for d5...\n";
    t.initialize(db, White);
    moves = {{chessx::Square::e2, chessx::Square::e4},
             {chessx::Square::e7, chessx::Square::e5},
             {chessx::Square::g1, chessx::Square::f3},
             {chessx::Square::b8, chessx::Square::c6},
             {chessx::Square::f1, chessx::Square::b5},
    };
    if (!play_through_line(t, moves))
    {
        std::cerr << "   Failed to play already-reviewed line\n";
        return false;
    }
    return true;
}

#define RUN_TEST(function_name, pgn) \
    { \
        MemoryDatabase db; \
        if (!db.openString(pgn)) \
        { \
            std::cerr << "Failed to open test training file\n"; \
            return; \
        } \
        if (!function_name(db)) \
            std::cerr << "FAILED TEST: " #function_name "\n"; \
        else \
            std::cout << "Test " #function_name " passed\n"; \
    }

void test_training(void)
{
    // memoryDatabase needs this global
    AppSettings = new ChessXSettings;
    AppSettings->setValue("/General/automaticECO", false);
    RUN_TEST(successful_review, starting_game)
    RUN_TEST(failed_review, starting_game)
    RUN_TEST(one_per_day, starting_game)
    RUN_TEST(progress_to_next_training, starting_game_one)
    RUN_TEST(sort_training_lines_oldest_to_review, starting_game_two)
    RUN_TEST(sort_training_lines_has_been_seen_first, starting_game_two)
    RUN_TEST(sort_training_lines_longest_first, starting_game)
    RUN_TEST(selects_new_lines, starting_game_three)
}
#endif
