#ifndef PROJECTOPTIONS_H
#define PROJECTOPTIONS_H

#include <QWidget>

#include "asideconfig.h"

namespace Ui {
    class Project;
}

class ProjectOptions : public QWidget
{
    Q_OBJECT

private:
    Ui::Project *ui;

public:
    static const QString compiler;
    static const QString memtype;
    static const QString optimization;
    static const QString cflags;
    static const QString lflags;

    explicit ProjectOptions(QWidget *parent = 0);
    ~ProjectOptions();

    void clearOptions();

    QString getCompiler();
    QString getMemModel();
    QString getOptimization();

    QString get32bitDoubles();
    QString getWarnAll();
    QString getNoFcache();
    QString getExceptions();
    QString getMathLib();
    QString getPthreadLib();
    QString getSimplePrintf();
    QString getStripElf();
    QString getCompOptions();
    QString getLinkOptions();

    void setCompiler(QString s);
    void setMemModel(QString s);
    void setOptimization(QString s);

    void set32bitDoubles(bool s);
    void setWarnAll(bool s);
    void setNoFcache(bool s);
    void setExceptions(bool s);
    void setMathLib(bool s);
    void setPthreadLib(bool s);
    void setSimplePrintf(bool s);
    void setStripElf(bool s);
    void setCompOptions(QString s);
    void setLinkOptions(QString s);

    QStringList getOptions();
    void        setOptions(QString s);

};

#endif // PROJECTOPTIONS_H