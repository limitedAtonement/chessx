#include "databaseinfo.h"
#include "training.h"
#include "database.h"
#include <iostream>

/** Reads all child nodes in the GameCursor and adds a training_line for
 * any leaf nodes found
 */
void get_training_lines(GameCursor & cursor, std::vector<MoveId> const & prefix, std::vector<training_line> & lines)
{
    std::cout << "  currently looking at " << cursor.variationCount() << " variations...\n";
    std::cout << "   cursor is at " << cursor.currMove() << '\n';
    if (!cursor.variationCount())
    {
        std::cout << "   got a leaf\n";
    }
    for (MoveId const & variation_move : cursor.variations(CURRENT_MOVE))
    {
        get_training_lines(cursor, variation_move, lines);
    }
}

void Training::initialize(DatabaseInfo & db_info)
{
    GameX temp_game;
    Database & db{*db_info.database()};
    std::cout << "Got database with " << db.count() << " games!\n";
    for (quint64 game_id{0}; game_id < db.count(); ++game_id)
    {
        if (!db.loadGame(game_id, temp_game))
        {
            std::cerr << "Failed to load game " << game_id << " continuing...\n";
            continue;
        }
        std::cout << "Game " << game_id << " has " << temp_game.cursor().countMoves() << " moves.\n";
        get_training_lines(temp_game.cursor(), ROOT_NODE, lines);
    }
}
