#include "ctags.h"
#include "mainwindow.h"

CTags::CTags(QString path, QObject *parent) : QObject(parent)
{
    QString ctags("ctags");

    compilerPath = path;
    ctagsProgram = path+ctags;

#if defined(Q_WS_WIN32)
    ctagsProgram = ctagsProgram+".exe";
#endif

    if(QFile::exists(ctagsProgram))
        ctagsFound = true;
    else
        ctagsFound = false;

    process = new QProcess();
}

int CTags::runCtags(QString path)
{
    int rc = -1;
    QStringList args;

    if(ctagsFound == false)
        return rc;

    /* if project file in path is not valid, return false
     */
    if(path.length() < 1)
        return rc;

    QFile proj(path);
    if(proj.exists() == false)
        return rc;

    if(proj.open(QFile::ReadOnly | QFile::Text) == false)
        return rc;

    QString pstr = proj.readAll();
    proj.close();

    /* add project files to ctags list so we don't zoom in unrelated files
     */
    QStringList plist = pstr.split("\n");
    for(int n = plist.length()-1; n >= 0; n--) {
        QString s = plist.at(n);
        if(s.length() < 1) {
            plist.removeAt(n);
            continue;
        }
        if(s.at(0) == '>') {
            plist.removeAt(n);
            continue;
        }
        if(s.at(0) == '-') {
            plist.removeAt(n);
            continue;
        }
    }

    projectPath = QDir::fromNativeSeparators(path);
    projectPath = projectPath.mid(0,projectPath.lastIndexOf("/")+1);

    connect(process, SIGNAL(readyReadStandardOutput()),this,SLOT(procReadyRead()));
    connect(process, SIGNAL(finished(int,QProcess::ExitStatus)),this,SLOT(procFinished(int,QProcess::ExitStatus)));
    connect(process, SIGNAL(error(QProcess::ProcessError)), this, SLOT(procError(QProcess::ProcessError)));

    process->setProcessChannelMode(QProcess::MergedChannels);
    process->setWorkingDirectory(projectPath);

    args.append("--format=1");

    /* append project files */
    foreach(QString argstr, plist) {
        if(argstr.length() > 0) {
            if(argstr.contains(FILELINK)) {
                argstr = argstr.mid(argstr.indexOf(FILELINK)+QString(FILELINK).length());
                args.append(argstr);
            }
            else {
                args.append(projectPath+argstr);
            }
        }
    }
    procDone = false;
    process->start(ctagsProgram,args);

    /* process Qt application events until procDone
     */
    while(procDone == false)
        QApplication::processEvents();

    rc = process->exitCode();
    return rc;
}

void CTags::procError(QProcess::ProcessError code)
{
    mutex.lock();
    procDone = true;
    mutex.unlock();
    if(code != 0)
        qDebug() << "ctags procError " << code;
}

void CTags::procReadyRead()
{
    // qDebug() << "procReadyRead :" << process->readAllStandardOutput();
}

void CTags::procFinished(int code, QProcess::ExitStatus status)
{
    if(procDone)
        return;

    mutex.lock();
    procDone = true;
    mutex.unlock();

    if((code != 0) | (status != 0))
        qDebug() << "runCtags procFinished " << code << " " << status << ":" << process->readAllStandardOutput();
}

bool CTags::enabled()
{
    return ctagsFound;
}

QString CTags::findTag(QString symbol)
{
    QString rets("");
    if(ctagsFound == false)
        return rets;

    QFile file(projectPath+"tags");
    if(file.exists() == false)
        return rets;

    QString fileString;
    if(file.open(QFile::ReadOnly)) {
        fileString = file.readAll();
        file.close();
    }
    if(fileString.contains(symbol)) {
        QStringList list = fileString.split("\n");
        QRegExp rx("^"+symbol+"[ \t].*");
        for(int n = 0; n < list.length(); n++) {
            QString line = list.at(n);
            if(line.length() == 0)
                continue;
            if(line.at(0) == '!')
                continue;
            int pos = rx.indexIn(line);
            if(pos > -1) {
                rets = line;
                break;
            }
        }
    }
    return rets;
}

QString CTags::getFile(QString line)
{
    QStringList item = line.split("\t");
    return item.at(1);
}

int CTags::getLine(QString line)
{
    int rc = -1;
    QStringList item = line.split("\t");
    QFile file(item.at(1));
    if(file.exists() == false)
        return rc;

    QString filestr;
    if(file.open(QFile::ReadOnly)) {
        filestr = file.readAll();
        file.close();
    }
    QString rspec = item.at(2);
    bool isnumber;
    int num = rspec.toInt(&isnumber);
    if(isnumber) {
        return num;
    }
    else {
        if(rspec.indexOf('^') > -1)
            rspec = rspec.mid(rspec.indexOf('^')+1);
        if(rspec.lastIndexOf('$') > 0)
            rspec = rspec.mid(0,rspec.lastIndexOf('$'));
        QRegExp rx(rspec,Qt::CaseSensitive, QRegExp::RegExp);
        QRegExp rx2(rspec,Qt::CaseSensitive, QRegExp::RegExp2);
        QRegExp rxwc(rspec,Qt::CaseSensitive, QRegExp::Wildcard);
        QRegExp rxwcu(rspec,Qt::CaseSensitive, QRegExp::WildcardUnix);
        QRegExp rxfs(rspec,Qt::CaseSensitive, QRegExp::FixedString);
        QRegExp rxw3(rspec,Qt::CaseSensitive, QRegExp::W3CXmlSchema11);
#if 0
        /* could return the file position of the search string */
        int pos = rxwc.indexIn(filestr);
        if(pos > -1)
            return pos;
#else
        QStringList list = filestr.split("\n");
        /* searching backwards increases chance of finding
         * the function definition instead of a declaration.
         */
        for(int n = list.length()-1; n >= 0; n--) {
            QString line = list.at(n)+"\n";
            int pos = rx.indexIn(line);
            pos &= rx2.indexIn(line);
            pos &= rxwc.indexIn(line);
            pos &= rxwcu.indexIn(line);
            pos &= rxfs.indexIn(line);
            pos &= rxw3.indexIn(line);
            if(pos > -1)
                return n;
        }
#endif
    }

    return rc;
}

int CTags::tagPush(QString tagline)
{
    tagStack.append(tagline);
    return tagStack.count();
}

QString CTags::tagPop()
{
    QString tagline;
    if(tagStack.count() < 1)
        return "";
    tagline = tagStack.at(tagStack.count()-1);
    tagStack.removeLast();
    return tagline;
}

void CTags::tagClear()
{
    tagStack.clear();
}

int CTags::tagCount()
{
    return tagStack.count();
}

