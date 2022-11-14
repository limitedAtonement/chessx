/***************************************************************************
 *   (C) 2006-2007 Marius Roets <roets.marius@gmail.com>                   *
 *   (C) 2007-2009 Michal Rudolf <mrudolf@kdewebdev.org>                  *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 ***************************************************************************/

#ifndef OUTPUT_H_INCLUDED
#define OUTPUT_H_INCLUDED

#include "gamex.h"
#include "outputoptions.h"
#include "database.h"
#include "filter.h"

#include <QtCore>

typedef QString (*BoardRenderingFunc)(const BoardX &b, QSize pxSize);

/** @ingroup Core
The Output class converts game to various formats.
Usage:
@code
GameX game;
PgnDatabase db;
db.loadGame(0,game);
Output o(Output::Html,"/usr/local/data/template-file.template");
o.output(&game);
@endcode
*/

class Output : public QObject
{
    Q_OBJECT
public:
    /** The different types of markup that can be used.
     * The settings for each is set in the template file.
     * @see setTemplateFile() */
    enum MarkupType
    {
        MarkupHeaderBlock,
        MarkupNotationBlock,
        MarkupResult,
        MarkupDiagram,
        MarkupNoFormat,
        MarkupColumnStyleMainline,
        MarkupColumnStyleMove,
        MarkupColumnStyleRow,
        MarkupMainLineMove,
        MarkupMainLine,
        MarkupVariationMove,
        MarkupVariationInline,
        MarkupVariationResume,
        MarkupVariationResume1,
        MarkupVariationResume2,
        MarkupVariationResume3,
        MarkupVariationResume4,
        MarkupVariationResume5,
        MarkupVariationResume6,
        MarkupVariationResume7,
        MarkupVariationResume8,
        MarkupVariationResume9,
        MarkupVariationIndent,
        MarkupVariationIndent1,
        MarkupNag,
        MarkupAnnotationInline,
        MarkupAnnotationIndent,
        MarkupPreAnnotationInline,
        MarkupPreAnnotationIndent,
        MarkupHeaderLine,
        MarkupHeaderTagName,
        MarkupHeaderTagValue,
        MarkupWhiteTag,
        MarkupBlackTag,
        MarkupEventTag,
        MarkupDateTag,
        MarkupSiteTag,
        MarkupResultTag,
        MarkupRoundTag,
        MarkupMate
    };
    /** The supported output types */
    enum OutputType
    {
        Html, /**< Exports the game in Html format */
        Pgn, /**< Exports the game in PGN format */
        Latex, /**< Exports the game in Latex format */
        NotationWidget /**< Exports the game in format appropriate for the notation widget */
    };
    enum MoveToWrite
    {
        PreviousMove,
        NextMove
    };
    enum CommentType
    {
        Precomment,
        Comment
    };
    /* enum CommentIndentOption {
     *    Always,
     *    OnlyMainline,
     *    Never
     * }; */
    /** Constructor.
     * Creates an output object for the given output type. Output can then
     * be generated by calling one of the output() methods
     * @param output The format of the output to be generated
     * @param pathToTemplateFile The full path to the file that contains the
     *        the template for the output to be generated
     * @see Output::OutputType */
    Output(OutputType output, BoardRenderingFunc renderer = nullptr, const QString& pathToTemplateFile = "");
    ~Output();

    /** Create the output for the given game
     * @return A string containing the game in the specified format
     * @param game A pointer to the game object being output */
    QString output(const GameX* game, bool upToCurrentMove = false);
    /** Create the output for the given game
     * @param filename The filename that the output will be written to.
     * @param filter A GameX object. Exported, using the output(GameX* game) method */
    void output(const QString& filename, const GameX& game);
    /** Create the output for the given filter
     * @param filename The filename that the output will be written to.
     * @param filter A Filter object. All games in the filter will be output, one
     *               after the other, using the output(GameX* game) method */
    void output(const QString& filename, FilterX& filter);
    /** Create the output for the given database
     * @param filename The filename that the output will be written to.
     * @param database A pointer to a database object. All games in the database will be output, one
     *               after the other, using the output(GameX* game) method */
    void output(const QString& filename, Database& database);
    /** Create the output for the given database
     * @param database A pointer to a database object. All games in the database will be output, one
     *               after the other, using the output(GameX* game) method */
    QString output(Database* database);

    /** Append output to a closed file */
    bool append(const QString& filename, GameX& game);
    /** Append a database to a closed file */
    void append(const QString& filename, Database& database);

    /** User definable settings.
     * Sets the filename of the file that contains the template that will be used
     * when creating the output. See example template files for syntax.
     * @param filename The full path to the file containing the template for the output */
    void setTemplateFile(QString filename = "");
    /** Static list of objects. */
    static QMap<OutputType, QString>& getFormats();

signals:
    /** Operation progress. */
    void progress(int);
protected:
    QString outputTags(const GameX *game);
private:
    /* User definable settings */
    OutputOptions m_options;
    /** The name of the current template file */
    QString m_templateFilename;

    /* Internally used */
    /** Function to render board into image */
    BoardRenderingFunc m_renderer;
    /** Text to be written at the top of the output */
    QString m_header;
    /** Text to be written at the bottom of the output */
    QString m_footer;
    /** The type of output that the object will generate */
    OutputType m_outputType;
    /** Indicator whether or not to write the move number, when it is black to move */
    bool m_dirtyBlack;
    /** Keep track of the current level of variation, for indent purposes */
    int m_currentVariationLevel;
    /** Character/string used for newline */
    QString m_newlineChar;
    /** Pointer to the game being exported */
    GameX m_game;
    /** Map containing the different types of outputs available, and a description of each */
    static QMap<OutputType, QString> m_outputMap;
    /** Map containing the start markup tag for each markup type */
    QMap<MarkupType, QString> m_startTagMap;
    /** Map containing the end markup tag for each markup type */
    QMap<MarkupType, QString> m_endTagMap;
    QMap<MarkupType, bool> m_expandable;

    /* Setting and retrieving of option. Methods to inteface
     * with OutputOptions class.
     */
    /* Setting values */
    /** Set option optionName to value optionValue */
    bool setOption(const QString& optionName, bool optionValue);
    /** Set option optionName to value optionValue */
    bool setOption(const QString& optionName, int optionValue);
    /** Set option optionName to value optionValue */
    bool setOption(const QString& optionName, const QString& optionValue);
    /* Retrieving values */
    /** Return the value of option optionName as integer */
    int getOptionAsInt(const QString& optionName);
    /** Return the value of option optionName as QString */
    QString getOptionAsString(const QString& optionName);
    /** Return the value of option optionName as boolean */
    bool getOptionAsBool(const QString& optionName);
    /** Return the description of option optionName */
    QString getOptionDescription(const QString& optionName);
    /** Return a list of all options */
    QStringList getOptionList();
    /** Sets the start and end tag for a certain markup type */
    void setMarkupTag(MarkupType type, const QString& startTag, const QString& endTag);
    /** Returns the start and end tag for a certain markup type in startTag and endTag */
    void markupTag(MarkupType type, QString& startTag, QString& endTag);
    /** Read tag settings from user settings */
    void readConfig();
    /** Write tag settings to user settings */
    void writeConfig();
    /** Read the template file */
    void readTemplateFile(const QString& path);
    /** Sets the default settings for the specific output format */
    void initialize();
    /** Reload default tag settings */
    void reset();

    /** Create the output for the given filter
     * @param out A textstream that will be used to write the results to
     * @param filter A Filter object. All games in the filter will be output, one
     *               after the other, using the output(GameX* game) method */
    void output(QTextStream& out, FilterX& filter);
    /** Create the output for the given database
     * @param out A textstream that will be used to write the results to
     * @param database A pointer to a database object. All games in the database will be output, one
     *               after the other, using the output(GameX* game) method */
    void output(QTextStream& out, Database& database);

    /** Output of a single game - requires postProcessing */
    QString outputGame(const GameX *g, bool upToCurrentMove);
    /** postProcessing of a game output or a dataBase output */
    void postProcessOutput(QString& text) const;

    /* Writing Methods */
    /** writes a comment associated with a game with no moves */
    QString writeGameComment(QString comment) const;
    /** Writes a diagram */
    QString writeDiagram(int n) const;
    /** Writes a single move including nag and annotation */
    QString writeMove(MoveToWrite moveToWrite = NextMove);
    /** Writes a variation, including sub variations */
    QString writeMainLine(MoveId upToNode);
    /** Writes a variation, including sub variations */
    QString writeVariation();
    /** Writes a game tag */
    QString writeTag(const QString& tagName, const QString& tagValue) const;
    /** Writes all game tags */
    QString writeAllTags() const;
    /** Writes basic Tags for HTML */
    QString writeBasicTagsHTML() const;
    /** Writes comment. @p mvno keeps a string representing move number (used for indentation. */
    QString writeComment(const QString& comment, const QString& mvno, CommentType type = Comment);

};

#endif

