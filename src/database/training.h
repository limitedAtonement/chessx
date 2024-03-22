#pragma once
#include "databaseinfo.h"

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
};

struct Training
{
    void initialize(DatabaseInfo &);
private:
    std::vector<training_line> lines;
};
