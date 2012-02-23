/*
 * Replace is similar to Find.
 */

#ifndef REPLACEDIALOG_H
#define REPLACEDIALOG_H

#include <QtGUI>

class ReplaceDialog : public QDialog
{
    Q_OBJECT

public:
    ReplaceDialog(QWidget *parent = 0);
    QTextDocument::FindFlag getFlags(int prev = 0);

    QString getFindText();
    void    clearFindText();
    QString getReplaceText();
    void    clearReplaceText();
    void    setFindText(QString text);

    bool showBeginMessage(QString type);
    bool showEndMessage(QString type);

    void setEditor(QPlainTextEdit *ed);

public slots:
    void findClicked();
    void findNextClicked();
    void findPrevClicked();
    void replaceClicked();
    void replaceNextClicked();
    void replacePrevClicked();
    void replaceAllClicked();
    void accept();

private:
    QPlainTextEdit *editor;

    QToolButton *findNextButton;
    QToolButton *findPrevButton;
    QToolButton *replaceNextButton;
    QToolButton *replacePrevButton;
    QPushButton *replaceAllButton;
    QLineEdit   *findEdit;
    QString     findText;
    QLineEdit   *replaceEdit;
    QString     replaceText;

    QPushButton *okButton;

    QToolButton *caseSensitiveButton;
    QToolButton *wholeWordButton;

    QGridLayout *layout;
};

#endif
// REPLACEDIALOG_H
