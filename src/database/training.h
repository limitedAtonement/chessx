#pragma once
#include "databaseinfo.h"

void test_training();

/** @ingroup Database
 * The training class is a spaced repetition training engine.
 *
 * This training engine helps players learn moves in a database.
 * It keeps track of training progress through comments in the games themselves
 * which should maximize portability and continuity should the player change
 * computers.
 *
 * Each leaf node of all selected chess games (with their variants) is a distinct
 * line for memorization, and progress is tracked from that point.
 */

struct training_line
{
    std::vector<Move> moves;
    bool has_been_seen;
    std::time_t first_reviewed;
    std::time_t last_reviewed;
    std::time_t next_review;
    MoveId leaf_id;
    GameX * game {nullptr};
    GameId game_id;
};

struct Training
{
    // initial_increment is the amount of time between the first review of
    // a line and the next review. (Subsequent review times are based on this.)
    Training(unsigned new_lines_per_day = 1, std::time_t initial_increment = 20);
    void initialize(Database&, Color);
    // If the move is correct according to the current training line
    bool move(Move const &);
    // This should be used to tell the trainer how to respond to the
    // trainee's last move.
    // If the trainee moves first and hasn't moved yet, returns an Invalid move.
    Move last_response(void) const;
    // After training is finished, the associated game is updated with an annotation
    // recording training progress. Get that game from `get_game()` and update
    // the database with it.
    bool finished_current_training(void) const;
    bool done_training_today(void) const;
    // Returns the current Game used for training. If no game is currently being used
    // (training hasn't begun, hasn't been initialized, etc.), nullptr is returned.
    GameX * get_game(void);
    std::optional<GameId> get_game_id(void) const;
#if TRAINING_TEST
    std::vector<training_line> & get_lines(void);
#endif
private:
    std::vector<training_line> lines;
    Color training_color = White;
    std::size_t current_move_in_line{0};
    bool missed_any_this_time{false};
    // We need to store the games somewhere so that the training_lines can retain
    // a reference to them.
    std::vector<GameX> games;
    unsigned new_lines_per_day;
    std::time_t initial_increment;

    void initialize_impl(void);
    bool handle_done();
};
