#define TRAINING_TEST 1
#include "databaseinfo.h"
#include "training.h"
#include "database.h"
#include <QDateTime>
#include <iostream>

static std::time_t get_last_reviewed_from_annotation(QString const & annotation)
{
    return 0;
}

static std::time_t get_first_reviewed_from_annotation(QString const & annotation)
{
    return 0;
}

static uint32_t get_bin_from_annotation(QString const & annotation)
{
    return 0;
}

static uint32_t get_correct_from_annotation(QString const & annotation)
{
    return 0;
}

static training_line get_training_line(GameX & game)
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
    uint32_t bin = get_bin_from_annotation(annotation);
    std::time_t last_reviewed = get_last_reviewed_from_annotation(annotation);
    std::time_t first_reviewed = get_first_reviewed_from_annotation(annotation);
    uint32_t correct_in_a_row = get_correct_from_annotation(annotation);
    return {std::move(moves), bin, last_reviewed, first_reviewed, correct_in_a_row, game.currentMove(), game};
}

/** Reads all child nodes in the game and adds a training_line for
 * any leaf nodes found
 */
void get_training_lines(GameX & game, std::vector<training_line> & lines)
{
    if (game.nextMove() == NO_MOVE)
    {
        lines.emplace_back(get_training_line(game));
        return;
    }
    game.forward();
    get_training_lines(game, lines);
    game.backward();
    if (!game.variationCount())
        return;
    for (MoveId const & variation_move : game.variations())
    {
        game.enterVariation(variation_move);
        get_training_lines(game, lines);
        game.backward();
    }
}

void Training::initialize(Database & db, Color color)
{
    std::cout << "Got database with " << db.count() << " games!\n";
    for (quint64 game_id{0}; game_id < db.count(); ++game_id)
    {
        games.emplace_back();
        if (!db.loadGame(game_id, games.back()))
        {
            std::cerr << "Failed to load game " << game_id << " continuing...\n";
            continue;
        }
        games.back().cursor().moveToStart();
        std::cout << "Game " << game_id << " has " << games.back().cursor().countMoves() << " moves.\n";
        get_training_lines(games.back(), lines);
    }
    this->training_color = color;
}

bool Training::move(Move const & new_move)
{
    try
    {
        std::cout << "Trying move " << new_move.toAlgebraicDebug().toStdString() << '\n';
        Move const & expected_move {lines.at(current_line).moves.at(current_move_in_line)};
        std::cout << "Expecting " << expected_move.toAlgebraicDebug().toStdString() << '\n';
        if (expected_move.to() == new_move.to() && expected_move.from() == new_move.from())
        {
            std::cout << "  all good!\n";
            next_move();
            handle_done();
            return true;
        }
        std::cout << "  not good!\n";
        missed_any_this_time = true;
        return false;
    }
    catch (std::out_of_range const &)
    {
        return false;
    }
    return true;
}

Move Training::next_move(void)
{
    try
    {
        return lines.at(current_line).moves.at(current_move_in_line++);
    }
    catch (std::out_of_range const &)
    {
        return {chessx::Square::InvalidSquare, chessx::Square::InvalidSquare};
    }
}

static constexpr char const * datetime_format = "yyyy-MM-dd";
// Returns true if we're done training this line, handles bookkeeping
// to record training progress on the current line, and resets the training object.
bool Training::handle_done()
{
    if (!finished())
        return false;
    try
    {
        training_line & line = lines.at(current_line);
        line.last_reviewed = std::time(nullptr);
        if (!missed_any_this_time)
        {
            ++line.correct_in_a_row;
            if (line.correct_in_a_row > 2)
            {
                line.correct_in_a_row = 0;
                ++line.bin;
            }
        }
        else
        {
            missed_any_this_time = false;
            line.correct_in_a_row = 0;
            if (line.bin)
                --line.bin;
        }
        QDateTime last_reviewed = QDateTime::fromSecsSinceEpoch(line.last_reviewed);
        QDateTime first_reviewed = QDateTime::fromSecsSinceEpoch(line.first_reviewed);
        QString new_annotation = QString{"bin: %1; last reviewed: %2; first reviewed: %3; correct in a row: %4"}
                .arg(line.bin).arg(last_reviewed.toString(datetime_format)).arg(first_reviewed.toString(datetime_format))
                .arg(line.correct_in_a_row);
        lines.at(current_line).game.dbSetAnnotation(new_annotation, GameX::Position::BeforeMove);
    }
    catch (std::out_of_range const &)
    {
        std::cerr << "Failed to handle training finished due to out of range error.\n";
    }
    return true;
}

bool Training::finished(void)
{
    try
    {
        return current_move_in_line >= lines.at(current_line).moves.size()-1;
    }
    catch (std::out_of_range const &)
    {
        return true;
    }
}

#if TRAINING_TEST
#include "pgndatabase.h"
#include "settings.h"
#include "chessxsettings.h"
void test_review(PgnDatabase & db)
{
    Training t;
    std::cout << "  test review...\n";
    t.initialize(db, White);
    std::cout << " initialized\n";
    if (!t.move({chessx::Square::e2, chessx::Square::e4}))
    {
        std::cerr << "e4 should be fine\n";
        return;
    }
    std::cout << "successfultest\n";
}

void test_training(void)
{
    std::cout << "testing training\n";
    AppSettings = new ChessXSettings;
    PgnDatabase db;
    if (!db.open("/home/lawsa/Documents/chessdata/my white openings.pgn", true))
    {
        std::cout << "Failed to open test training file\n";
        return;
    }
    if (!db.parseFile())
    {
        std::cout << "Failed to parse test training file\n";
        return;
    }
    test_review(db);
}
#endif
