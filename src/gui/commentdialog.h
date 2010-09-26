/****************************************************************************
*   Copyright (C) 2010 by Michal Rudolf <mrudolf@kdewebdev.org>           *
****************************************************************************/

#ifndef COMMENTDIALOG_H
#define COMMENTDIALOG_H

#include "ui_commentdialog.h"

class CommentDialog : public QDialog {
	 Q_OBJECT
public:
	 CommentDialog(QWidget *parent = 0);
	 void setText(const QString& text);
	 QString text() const;

private:
	 Ui::CommentDialog ui;
};

#endif // COMMENTDIALOG_H