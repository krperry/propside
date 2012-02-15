#include "mainwindow.h"
#include "qextserialenumerator.h"

#define APPWINDOW_MIN_HEIGHT 500
#define EDITOR_MIN_WIDTH 500
#define PROJECT_WIDTH 220

#define COMPILE_STATUS_TERMINAL 0

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent)
{
    /* setup application registry info */
    QCoreApplication::setOrganizationName(publisherKey);
    QCoreApplication::setOrganizationDomain(publisherComKey);
    QCoreApplication::setApplicationName(ASideGuiKey);

    /* global settings */
    settings = new QSettings(publisherKey, ASideGuiKey, this);

    /* setup properties dialog */
    propDialog = new Properties(this);
    connect(propDialog,SIGNAL(accepted()),this,SLOT(propertiesAccepted()));

    /* setup find dialog */
    findDialog = new FindDialog(this);

    /* new ASideConfig class */
    aSideConfig = new ASideConfig();

    projectModel = NULL;
    referenceModel = NULL;

    /* setup gui components */
    setupFileMenu();
    setupHelpMenu();
    setupToolBars();

    /* main container */
    setWindowTitle(ASideGuiKey);
    QSplitter *vsplit = new QSplitter(this);
    setCentralWidget(vsplit);

    /* minimum window height */
    this->setMinimumHeight(APPWINDOW_MIN_HEIGHT);

    /* project tools */
    setupProjectTools(vsplit);

    /* start with an empty file if fresh install */
    newFile();

    /* status bar for progressbar */
    QStatusBar *statusBar = new QStatusBar(this);
    this->setStatusBar(statusBar);
    progress = new QProgressBar();
    progress->setMaximumSize(90,20);
    progress->setValue(0);

    programSize = new QLabel();
    programSize->setMinimumWidth(21*10+8);
    status = new QLabel();
    status->setMinimumWidth(60*10);

    statusBar->addWidget(programSize);
    statusBar->addWidget(progress);
    statusBar->addWidget(status);
    //statusBar->setLayoutDirection(Qt::RightToLeft);
    statusBar->setMaximumHeight(22);

    /* get app settings at startup and before any compiler call */
    getApplicationSettings();

    initBoardTypes();

    /* setup the port listener */
    portListener = new PortListener();

    /* get available ports at startup */
    enumeratePorts();

    /* these are read once per app startup */
    QVariant lastportv  = settings->value(lastPortNameKey);
    if(lastportv.canConvert(QVariant::String))
        portName = lastportv.toString();

    /* setup the first port displayed in the combo box */
    if(cbPort->count() > 0) {
        int ndx = 0;
        if(portName.length() != 0) {
            for(int n = cbPort->count()-1; n > -1; n--)
                if(cbPort->itemText(n) == portName)
                {
                    ndx = n;
                    break;
                }
        }
        setCurrentPort(ndx);
    }

    /* setup the terminal dialog box */
    term = new Terminal(this);
    term->setWindowTitle(QString(ASideGuiKey)+" Terminal");
    connect(term,SIGNAL(accepted()),this,SLOT(terminalClosed()));
    connect(term,SIGNAL(rejected()),this,SLOT(terminalClosed()));

    /* tell port listener to use terminal editor for i/o */
    termEditor = term->getEditor();

#if COMPILE_STATUS_TERMINAL
    portListener->setTerminalWindow(compileStatus); // experimental mode
#else
    portListener->setTerminalWindow(termEditor);
#endif

    /* load the last file into the editor to make user happy */
    QVariant lastfilev = settings->value(lastFileNameKey);
    if(!lastfilev.isNull()) {
        if(lastfilev.canConvert(QVariant::String)) {
            QString fileName = lastfilev.toString();
            openFileName(fileName);
            setProject(); // last file is always first project
        }
    }

    // old hardware dialog configuration feature
    //hardwareDialog = new Hardware(this);
    //connect(hardwareDialog,SIGNAL(accepted()),this,SLOT(initBoardTypes()));
}

void MainWindow::keyHandler(QKeyEvent* event)
{
    //qDebug() << "MainWindow::keyHandler";
    int key = event->key();
    switch(key)
    {
    case Qt::Key_Enter:
    case Qt::Key_Return:
        key = '\n';
        break;
    case Qt::Key_Backspace:
        key = '\b';
        break;
    default:
        if(key & Qt::Key_Escape)
            return;
        QChar c = event->text().at(0);
        key = (int)c.toAscii();
        break;
    }
    QByteArray barry;
    barry.append((char)key);
    portListener->send(barry);
}

void MainWindow::sendPortMessage(QString s)
{
    QByteArray barry;
    foreach(QChar c, s) {
        barry.append(c);
        portListener->send(barry);
        barry.clear();
        this->thread()->yieldCurrentThread();
    }
}

void MainWindow::terminalEditorTextChanged()
{
    QString text = termEditor->toPlainText();
}

/*
 * get the application settings from the registry for compile/startup
 */
void MainWindow::getApplicationSettings()
{
    QFile file;
    QVariant compv = settings->value(compilerKey);

    if(compv.canConvert(QVariant::String))
        aSideCompiler = compv.toString();

    if(!file.exists(aSideCompiler))
    {
        propDialog->showProperties();
    }

    /* get the separator used at startup */
    QString appPath = QCoreApplication::applicationDirPath ();
    if(appPath.indexOf('\\') > -1)
        aSideSeparator = "\\";
    else
        aSideSeparator = "/";

    /* get the compiler path */
    if(aSideCompiler.indexOf('\\') > -1) {
        aSideCompilerPath = aSideCompiler.mid(0,aSideCompiler.lastIndexOf('\\')+1);
    }
    else if(aSideCompiler.indexOf('/') > -1) {
        aSideCompilerPath = aSideCompiler.mid(0,aSideCompiler.lastIndexOf('/')+1);
    }

    aSideLoader = aSideCompilerPath + "propeller-load.exe";

    /* get the include path and config file set by user */
    QVariant incv = settings->value(includesKey);
    QVariant cfgv = settings->value(configFileKey);

    /* convert registry values to strings */
    if(incv.canConvert(QVariant::String))
        aSideIncludes = incv.toString();

    if(cfgv.canConvert(QVariant::String))
        aSideCfgFile = cfgv.toString();

    if(!file.exists(aSideCfgFile))
    {
        propDialog->showProperties();
    }
    else
    {
        /* load boards in case there were changes */
        aSideConfig->loadBoards(aSideCfgFile);
    }
}

void MainWindow::exitSave()
{
    bool saveAll = false;
    QMessageBox mbox(QMessageBox::Question, "Save File?", "",
                     QMessageBox::Discard | QMessageBox::Save | QMessageBox::SaveAll, this);

    saveProjectOptions();

    for(int tab = editorTabs->count()-1; tab > -1; tab--)
    {
        QString tabName = editorTabs->tabText(tab);
        if(tabName.at(tabName.length()-1) == '*')
        {
            mbox.setInformativeText(tr("Save File: ") + tabName.mid(0,tabName.indexOf(" *")) + tr(" ?"));
            if(saveAll)
            {
                saveFileByTabIndex(tab);
            }
            else
            {
                int ret = mbox.exec();
                switch (ret) {
                    case QMessageBox::Discard:
                        // Don't Save was clicked
                        return;
                        break;
                    case QMessageBox::Save:
                        // Save was clicked
                        saveFileByTabIndex(tab);
                        break;
                    case QMessageBox::SaveAll:
                        // save all was clicked
                        saveAll = true;
                        break;
                    default:
                        // should never be reached
                        break;
                }
            }
        }
    }
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    portListener->close();

    exitSave(); // find
    QString fileName = "";

    if(projectFile.isEmpty()) {
        fileName = editorTabs->tabToolTip(editorTabs->currentIndex());
        if(!fileName.isEmpty())
            settings->setValue(lastFileNameKey,fileName);
    }
    else {
        QFile proj(projectFile);
        if(proj.open(QFile::ReadOnly | QFile::Text)) {
            fileName = sourcePath(projectFile)+proj.readLine();
            fileName = fileName.trimmed();
            proj.close();
        }
        settings->setValue(lastFileNameKey,fileName);
        saveProjectOptions();
    }

    QString boardstr = cbBoard->itemText(cbBoard->currentIndex());
    QString portstr = cbPort->itemText(cbPort->currentIndex());

    settings->setValue(lastBoardNameKey,boardstr);
    settings->setValue(lastPortNameKey,portstr);
}

void MainWindow::newFile()
{
    fileChangeDisable = true;
    setupEditor();
    int tab = editors->count()-1;
    editorTabs->addTab(editors->at(tab),(const QString&)untitledstr);
    editorTabs->setCurrentIndex(tab);
    fileChangeDisable = false;
}

void MainWindow::openFile(const QString &path)
{
    QString fileName = path;

    if (fileName.isNull()) {
        QFileDialog dialog(this);
        dialog.setDirectory(sourcePath(projectFile));
        fileName = dialog.getOpenFileName(this,
            tr("Open File"), "", "Program Source Files (*.c | *.cpp | *.h | *.cogc | *.spin | *.*)");
    }
    if(fileName.indexOf(".side") > 0) {
        // save old project options before loading new one
        saveProjectOptions();
        // load new project
        projectFile = fileName;
        setCurrentProject(projectFile);
        QFile proj(projectFile);
        if(proj.open(QFile::ReadOnly | QFile::Text)) {
            fileName = sourcePath(projectFile)+proj.readLine();
            fileName = fileName.trimmed();
            proj.close();
        }
        updateProjectTree(fileName,"");
    }
    openFileName(fileName);
    if(projectFile.length() == 0) {
        setProject();
    }
    else if(editorTabs->count() == 1) {
        setProject();
    }
}

void MainWindow::openFileName(QString fileName)
{
    QString data;
    if (!fileName.isEmpty()) {
        QFile file(fileName);
//        if (file.open(QFile::ReadOnly | QFile::Text))
        if (file.open(QFile::ReadOnly))
        {
            data = file.readAll();
            data.replace('\t',"    ");
            QString sname = this->shortFileName(fileName);
            if(editorTabs->count()>0) {
                for(int n = editorTabs->count()-1; n > -1; n--) {
                    if(editorTabs->tabText(n) == sname) {
                        setEditorTab(n, sname, fileName, data);
                        file.close();
                        return;
                    }
                }
            }
            if(editorTabs->tabText(0) == untitledstr) {
                setEditorTab(0, sname, fileName, data);
                file.close();
                return;
            }
            newFile();
            setEditorTab(editorTabs->count()-1, sname, fileName, data);
            file.close();
        }
    }
}


void MainWindow::openRecentProject(const QString &proj)
{
    QAction *action = qobject_cast<QAction *>(sender());
    if (action)
        openFile(action->data().toString());
}


void MainWindow::setCurrentProject(const QString &fileName)
{
    projectFile = fileName;
    //setWindowFilePath(curFile);

    QStringList files = settings->value("recentProjectList").toStringList();
    files.removeAll(fileName);
    files.prepend(fileName);
    while (files.size() > MaxRecentProjects)
        files.removeLast();

    settings->setValue("recentProjectList", files);

    foreach (QWidget *widget, QApplication::topLevelWidgets()) {
        MainWindow *mainWin = qobject_cast<MainWindow *>(widget);
        if (mainWin)
            mainWin->updateRecentProjectActions();
    }
}

void MainWindow::updateRecentProjectActions()
{
    QStringList projects = settings->value("recentProjectList").toStringList();

    int numRecentProjects = qMin(projects.size(), (int)MaxRecentProjects);

    for (int i = 0; i < numRecentProjects; ++i) {
        //QString filename = QFileInfo(projects[i]).fileName();
        QString text = tr("&%1 %2").arg(i + 1).arg(projects[i]);
        recentProjectActs[i]->setText(text);
        recentProjectActs[i]->setData(projects[i]);
        recentProjectActs[i]->setVisible(true);
    }
    for (int j = numRecentProjects; j < MaxRecentProjects; ++j)
        recentProjectActs[j]->setVisible(false);

    separatorAct->setVisible(numRecentProjects > 0);
}

void MainWindow::saveFile(const QString &path)
{
    try {
        int n = this->editorTabs->currentIndex();
        QString fileName = editorTabs->tabToolTip(n);
        QString data = editors->at(n)->toPlainText();

        this->editorTabs->setTabText(n,shortFileName(fileName));
        if (!fileName.isEmpty()) {
            QFile file(fileName);
            if (file.open(QFile::WriteOnly | QFile::Text)) {
                file.write(data.toAscii());
                file.close();
            }
        }
        saveProjectOptions();
    } catch(...) {
    }
}

void MainWindow::saveFileByTabIndex(int tab)
{
    try {
        QString fileName = editorTabs->tabToolTip(tab);
        QString data = editors->at(tab)->toPlainText();

        this->editorTabs->setTabText(tab,shortFileName(fileName));
        if (!fileName.isEmpty()) {
            QFile file(fileName);
            if (file.open(QFile::WriteOnly | QFile::Text)) {
                file.write(data.toAscii());
                file.close();
            }
        }
    } catch(...) {
    }
}

void MainWindow::saveAsFile(const QString &path)
{
    try {
        int n = this->editorTabs->currentIndex();
        QString data = editors->at(n)->toPlainText();
        QString fileName = path;

        if (fileName.isEmpty())
            fileName = QFileDialog::getSaveFileName(this,
                tr("Save As File"), "", "Program Source Files (*.c | *.cpp | *.h | *.cogc | *.spin | *.*)");

        if (fileName.isEmpty())
            return;

        this->editorTabs->setTabText(n,shortFileName(fileName));
        editorTabs->setTabToolTip(n,fileName);

        if (!fileName.isEmpty()) {
            QFile file(fileName);
            if (file.open(QFile::WriteOnly | QFile::Text)) {
                file.write(data.toAscii());
                file.close();
            }
        }
    } catch(...) {
    }
}

void MainWindow::fileChanged()
{
    if(fileChangeDisable)
        return;

    int index = editorTabs->currentIndex();
    QString name = editorTabs->tabText(index);
    QPlainTextEdit *ed = editors->at(index);
    QTextCursor cur = ed->textCursor();
    int cpos = cur.position();
    if(cpos > 0) {
        QString txt = ed->toPlainText();
        char ch = txt[cpos-1].toAscii();
        if(ch == '\t') {
            int n = cur.columnNumber() % 4;
            fileChangeDisable = true;
            if(n) {
                cur.movePosition(QTextCursor::Left,QTextCursor::KeepAnchor);
                cur.removeSelectedText();
                for(; n <= 4; n++) {
                    ed->insertPlainText(" ");
                }
            }
            fileChangeDisable = false;
            return; // just do tabs
        }
    }

    QString curtext = ed->toPlainText();
    QString fileName = editorTabs->tabToolTip(index);
    if(!QFile::exists(fileName))
        return;
    QFile file(fileName);
    if(file.open(QFile::ReadOnly))
    {
        QString text = file.readAll();
        file.close();
        if(text == curtext) {
            if(name.at(name.length()-1) == '*')
                editorTabs->setTabText(index, this->shortFileName(fileName));
            return;
        }
    }
    if(name.at(name.length()-1) != '*') {
        name += tr(" *");
        editorTabs->setTabText(index, name);
    }
}

void MainWindow::printFile(const QString &path)
{
    /*
    QString fileName = path;
    QString data = editor->toPlainText();

    if (!fileName.isEmpty()) {
    }
    */
}

void MainWindow::zipFile(const QString &path)
{
    /*
    QString fileName = path;
    QString data = editor->toPlainText();

    if (!fileName.isEmpty()) {
    }
    */
}

void MainWindow::copyFromFile()
{
    QPlainTextEdit *editor = editors->at(editorTabs->currentIndex());
    if(editor)
        editor->copy();
}
void MainWindow::cutFromFile()
{
    QPlainTextEdit *editor = editors->at(editorTabs->currentIndex());
    if(editor)
        editor->cut();
}
void MainWindow::pasteToFile()
{
    QPlainTextEdit *editor = editors->at(editorTabs->currentIndex());
    if(editor)
        editor->paste();
}
void MainWindow::editCommand()
{

}
void MainWindow::systemCommand()
{

}
void MainWindow::findInFile()
{
    if(!findDialog)
        return;

    findDialog->clearFindText();

    QPlainTextEdit *editor = editors->at(editorTabs->currentIndex());
    if(editor) {
        findDialog->show();
        findDialog->raise();
        findDialog->activateWindow();

        if (findDialog->exec() == QDialog::Accepted) {
            QString text = findDialog->getFindText();
            QString edtext = editor->toPlainText();
            if (edtext.contains(text,Qt::CaseInsensitive)) {
                editor->setCenterOnScroll(true);
                editor->find(text);
            } else {
                QMessageBox::information(this, tr("Text Not Found"),
                    tr("Can't find text: \"%1\"").arg(text));
                return;
            }
        }
    }
}

void MainWindow::findNextInFile()
{
    if(!findDialog)
        return;

    QPlainTextEdit *editor = editors->at(editorTabs->currentIndex());
    if(editor) {
        findDialog->show();
        findDialog->raise();
        findDialog->activateWindow();

        if (findDialog->exec() == QDialog::Accepted) {
            QString text = findDialog->getFindText();
            QString edtext = editor->toPlainText();
            if (edtext.contains(text,Qt::CaseInsensitive)) {
                editor->setCenterOnScroll(true);
                editor->find(text);
            } else {
                QMessageBox::information(this, tr("Text Not Found"),
                    tr("Can't find text: \"%1\"").arg(text));
                return;
            }
        }
    }
}
void MainWindow::findPrevInFile()
{
}

void MainWindow::redoChange()
{
    QPlainTextEdit *editor = editors->at(editorTabs->currentIndex());
    if(editor)
        editor->redo();
}
void MainWindow::undoChange()
{
    QPlainTextEdit *editor = editors->at(editorTabs->currentIndex());
    if(editor)
        editor->undo();
}


void MainWindow::setProject()
{
    int index = editorTabs->currentIndex();
    QString fileName = editorTabs->tabToolTip(index);
    if(fileName.length() != 0)
    {
        updateProjectTree(fileName,"");
        setCurrentProject(projectFile);
    }
    else {
        delete projectModel;
        projectModel = NULL;
    }
}

void MainWindow::hardware()
{
    //hardwareDialog->loadBoards();
    //hardwareDialog->show();
    initBoardTypes();
}

void MainWindow::properties()
{
    propDialog->showProperties();
}
void MainWindow::propertiesAccepted()
{
    getApplicationSettings();
    initBoardTypes();
}

void MainWindow::programBuild()
{
    runBuild();
}

void MainWindow::programBurnEE()
{
    if(runBuild())
        return;
    runLoader("-e");
}

void MainWindow::programRun()
{
    if(runBuild())
        return;
    runLoader("-r");
}

void MainWindow::programDebug()
{
    if(runBuild())
        return;
    if(runLoader("-r"))
        return;
    // don't start terminal if compiler or loader failed

    /*
     * setting the position of a new dialog doesn't work very nice
     * Term dialog will not close/reopen on debug so it doesn't matter.
     */
#if COMPILE_STATUS_TERMINAL
#else
    term->show();   // show if hidden
#endif

    btnConnected->setChecked(true);
    connectButton();
}

void MainWindow::terminalClosed()
{
    portListener->close();
    btnConnected->setChecked(false);
}

void MainWindow::setupHelpMenu()
{
    QMenu *helpMenu = new QMenu(tr("&Help"), this);
    menuBar()->addMenu(helpMenu);

    helpMenu->addAction(QIcon(":/images/helpsymbol.png"), tr("&About"), this, SLOT(about()));
}

void MainWindow::about()
{
    QString version = QString("PropGCC-SimpleIDE Version %1.%2.%3")
            .arg(IDEVERSION).arg(MINVERSION).arg(FIXVERSION);
    QMessageBox::about(this, tr("About PropGCC-SimpleIDE"), version + \
        tr("<p><b>PropGCC-SimpleIDE</b> manages Propeller GCC program builds, and <br/>" \
           "downloads programs to propeller for many basic board models.</p>") +
        tr("Visit <a href=\"www.MicroCSource.com/PropGCC-SimpleIDE/help.htm\">MicroCSource.com</a> for more PropGCC-SimpleIDE help."));
}


void MainWindow::setCurrentBoard(int index)
{
    boardName = cbBoard->itemText(index);
    cbBoard->setCurrentIndex(index);
}

void MainWindow::setCurrentPort(int index)
{
    portName = cbPort->itemText(index);
    cbPort->setCurrentIndex(index);
    if(portName.length()) {
        portListener->init(portName, BAUD115200);  // signals get hooked up internally
    }
}

void MainWindow::checkAndSaveFiles()
{
    if(projectModel == NULL)
        return;

    saveProjectOptions();

    QString title = projectModel->getTreeName();
    QString modTitle = title + " *";
    for(int tab = editorTabs->count()-1; tab > -1; tab--)
    {
        QString tabName = editorTabs->tabText(tab);
        if(tabName == modTitle)
        {
            saveFileByTabIndex(tab);
            editorTabs->setTabText(tab,title);
        }
    }

    int len = projectModel->rowCount();
    for(int n = 0; n < len; n++)
    {
        QModelIndex root = projectModel->index(n,0);
        QVariant vs = projectModel->data(root, Qt::DisplayRole);
        if(!vs.canConvert(QVariant::String))
            continue;
        QString name = vs.toString();
        QString modName = name + " *";
        for(int tab = editorTabs->count()-1; tab > -1; tab--)
        {
            QString tabName = editorTabs->tabText(tab);
            if(tabName == modName)
            {
                saveFileByTabIndex(tab);
                editorTabs->setTabText(tab,name);
            }
        }
    }
}

int  MainWindow::checkCompilerInfo()
{
    QMessageBox mbox(QMessageBox::Critical,tr("Build Error"),"",QMessageBox::Ok);
    if(aSideCompiler.length() == 0) {
        mbox.setInformativeText(tr("Please specify compiler application in properties."));
        mbox.exec();
        return -1;
    }
    if(aSideIncludes.length() == 0) {
        mbox.setInformativeText(tr("Please specify loader folder in properties."));
        mbox.exec();
        return -1;
    }
    return 0;
}

QString MainWindow::sourcePath(QString srcpath)
{
    srcpath = QDir::fromNativeSeparators(srcpath);
    srcpath = srcpath.mid(0,srcpath.lastIndexOf(aSideSeparator)+1);
    return srcpath;
}

int  MainWindow::runBuild(void)
{
    //QString srcpath = sourcePath(projectFile);
    QStringList clist;
    QFile file(projectFile);
    QString proj = "";
    if(file.open(QFile::ReadOnly | QFile::Text)) {
        proj = file.readAll();
        file.close();
    }

    programSize->setText("");

    compileStatus->setPlainText(tr("Project Directory: ")+sourcePath(projectFile)+"\n");
    status->setText(tr("Building with"));

    proj = proj.trimmed(); // kill extra white space
    QStringList list = proj.split("\n");

    for(int n = 0; n < list.length(); n++) {
        if(!list[n].length())
            continue;
        QString name = list[n];
        QString base = shortFileName(name.mid(0,name.lastIndexOf(".")));
        if(name.toLower().lastIndexOf(".spin") > 0) {
            if(runBstc(name))
                return -1;
            if(proj.toLower().lastIndexOf(".dat") < 0) // intermediate
                list.append((name.mid(0,name.lastIndexOf(".spin"))+".dat"));
        }
        else if(name.toLower().lastIndexOf(".dat") > 0) {
            if(runObjCopy(name))
                return -1;
            if(proj.toLower().lastIndexOf("_firmware.o") < 0)
                clist.append(name.mid(0,name.lastIndexOf(".dat"))+"_firmware.o");
        }
        else if(name.toLower().lastIndexOf(".s") > 0) {
            if(runGAS(name))
                return -1;
            if(proj.toLower().lastIndexOf(".o") < 0)
                clist.append(name.mid(0,name.lastIndexOf(".s"))+".o");
        }
        /* .cogc also does COG specific objcopy */
        else if(name.toLower().lastIndexOf(".cogc") > 0) {
            if(runCOGC(name))
                return -1;
            clist.append(base+".cog");
        }
        else {
            clist.append(name);
        }
    }
    if(!runCompiler(clist)) {
        status->setText(status->text()+tr(" done."));
        return 0;
    }
    return -1;
}

int  MainWindow::runCOGC(QString name)
{
    QString base = shortFileName(name.mid(0,name.lastIndexOf(".")));
    // copy .cog to .c
    // QFile::copy(sourcePath(projectFile)+name,sourcePath(projectFile)+base+".c");
    // run C compiler on new file
    QStringList args;
    args.append("-r");  // relocatable ?
    args.append("-Os"); // default optimization for -mcog
    args.append("-mcog"); // compile for cog
    args.append("-o"); // create a .cog object
    args.append(base+".cog");
    args.append("-xc"); // code to compile is C code
    //args.append("-c");
    args.append(name);

    QString compstr = shortFileName(aSideCompiler);
    QProcess proc(this);
    showBuildStart(compstr,args);

    proc.setWorkingDirectory(sourcePath(projectFile));
    proc.start(compstr,args);

    if(checkBuildStart(&proc, compstr))
        return -1;

    QString result = proc.readAll();
    result += proc.readAllStandardOutput();
    result += proc.readAllStandardError();
    if(result.length()>0)
        compileStatus->appendPlainText(result);

    if(result.length())
        return -1;

    /* now do objcopy */
    args.clear();
    args.append("--localize-text");
    args.append("--rename-section");
    args.append(".text="+base+".cog");
    args.append(base+".cog");

    /* old way */
    /*
    args.clear();
    args.append("--localize-text");
    args.append(base+".o");
    args.append(base+".cog");
    */

    //QString objcopy = sourcePath(aSideCompiler)+"propeller-elf-objcopy";
    QString objcopy = "propeller-elf-objcopy";
    showBuildStart(objcopy,args);

    proc.setWorkingDirectory(sourcePath(projectFile));
    proc.start(objcopy,args);

    if(checkBuildStart(&proc, objcopy))
        return -1;

    result = proc.readAll();
    result += proc.readAllStandardOutput();
    result += proc.readAllStandardError();
    if(result.length()>0)
        compileStatus->appendPlainText(result);

    if(result.length())
        return -1;

    return 0;
}

int  MainWindow::runBstc(QString spinfile)
{
    getApplicationSettings();
    if(checkCompilerInfo()) {
        return -1;
    }
    progress->setValue(0);

    QStringList args;
    args.append("-c");
    args.append(shortFileName(spinfile));
    //args.append("-o");
    //args.append(spinfile.mid(0,spinfile.lastIndexOf("."))+".dat");

    btnConnected->setChecked(false);
    portListener->close(); // disconnect uart before use

    progress->setValue(5);

    checkAndSaveFiles();
    progress->setValue(10);

    QString bstc = "bstc";
    showBuildStart(bstc,args);

    QProcess proc(this);

    proc.setWorkingDirectory(sourcePath(projectFile));
    proc.start(bstc,args);

    progress->setValue(15);

    if(checkBuildStart(&proc, bstc))
        return -1;

    progress->setValue(60);

    QString result = proc.readAll();
    result += proc.readAllStandardOutput();
    result += proc.readAllStandardError();
    if(result.length()>0)
        compileStatus->appendPlainText(result);

    int exitCode = proc.exitCode();
    int exitStatus = proc.exitStatus();
    int rc = buildResult(exitStatus, exitCode, bstc, result);

    progress->setValue(100);
    return rc;
}


int  MainWindow::runCogObjCopy(QString datfile)
{
    getApplicationSettings();
    if(checkCompilerInfo()) {
        return -1;
    }

    progress->setValue(0);

    QStringList args;

    args.append("--localize-text");
    args.append("--rename-section");
    args.append(".text="+datfile);
    args.append(datfile);

    btnConnected->setChecked(false);
    portListener->close(); // disconnect uart before use

    progress->setValue(5);

    checkAndSaveFiles();
    progress->setValue(10);

    //QString objcopy = sourcePath(aSideCompiler)+"propeller-elf-objcopy";
    QString objcopy = "propeller-elf-objcopy";
    showBuildStart(objcopy,args);

    QProcess proc(this);

    proc.setWorkingDirectory(sourcePath(projectFile));
    proc.start(objcopy,args);

    progress->setValue(15);

    if(checkBuildStart(&proc, objcopy))
        return -1;

    progress->setValue(60);

    QString result = proc.readAll();
    result += proc.readAllStandardOutput();
    result += proc.readAllStandardError();
    if(result.length()>0)
        compileStatus->appendPlainText(result);

    int exitCode = proc.exitCode();
    int exitStatus = proc.exitStatus();
    int rc = buildResult(exitStatus, exitCode, objcopy, result);
    progress->setValue(100);
    return rc;
}

int  MainWindow::runObjCopy(QString datfile)
{
    getApplicationSettings();
    if(checkCompilerInfo()) {
        return -1;
    }

    progress->setValue(0);

    QString objfile = datfile.mid(0,datfile.lastIndexOf("."))+"_firmware.o";
    QStringList args;
    args.append("-I");
    args.append("binary");
    args.append("-B");
    args.append("propeller");
    args.append("-O");
    args.append("propeller-elf-gcc");
    args.append(datfile);
    args.append(objfile);

    btnConnected->setChecked(false);
    portListener->close(); // disconnect uart before use

    progress->setValue(5);

    checkAndSaveFiles();
    progress->setValue(10);

    //QString objcopy = sourcePath(aSideCompiler)+"propeller-elf-objcopy";
    QString objcopy = "propeller-elf-objcopy";
    showBuildStart(objcopy,args);

    QProcess proc(this);

    proc.setWorkingDirectory(sourcePath(projectFile));
    proc.start(objcopy,args);

    progress->setValue(15);

    if(checkBuildStart(&proc, objcopy))
        return -1;

    progress->setValue(60);

    QString result = proc.readAll();
    result += proc.readAllStandardOutput();
    result += proc.readAllStandardError();
    if(result.length()>0)
        compileStatus->appendPlainText(result);

    int exitCode = proc.exitCode();
    int exitStatus = proc.exitStatus();
    int rc = buildResult(exitStatus, exitCode, objcopy, result);
    progress->setValue(100);
    return rc;
}

int  MainWindow::runGAS(QString gasfile)
{
    getApplicationSettings();
    if(checkCompilerInfo()) {
        return -1;
    }

    progress->setValue(0);

    QString objfile = gasfile.mid(0,gasfile.lastIndexOf("."))+".o";
    QStringList args;
    args.append("-o");
    args.append(objfile);
    args.append(gasfile);

    btnConnected->setChecked(false);
    portListener->close(); // disconnect uart before use

    progress->setValue(5);

    checkAndSaveFiles();
    progress->setValue(10);

    //QString gas = sourcePath(aSideCompiler)+"propeller-elf-as";
    QString gas = "propeller-elf-as";
    showBuildStart(gas,args);

    QProcess proc(this);

    proc.setWorkingDirectory(sourcePath(projectFile));
    proc.start(gas,args);

    progress->setValue(15);

    if(checkBuildStart(&proc, gas))
        return -1;

    progress->setValue(60);

    QString result = proc.readAll();
    result += proc.readAllStandardOutput();
    result += proc.readAllStandardError();
    if(result.length()>0)
        compileStatus->appendPlainText(result);

    int exitCode = proc.exitCode();
    int exitStatus = proc.exitStatus();
    int rc = buildResult(exitStatus, exitCode, gas, result);
    progress->setValue(100);
    return rc;
}

QStringList MainWindow::getCompilerParameters(QStringList copts)
{
    // use the projectFile instead of the current tab file
    QString srcpath = sourcePath(projectFile);

    portName = cbPort->itemText(cbPort->currentIndex());    // TODO should be itemToolTip
    boardName = cbBoard->itemText(cbBoard->currentIndex());

    QStringList args;
    args.append("-o");
    args.append("a.out");
    args.append(projectOptions->getOptimization());
    args.append("-m"+projectOptions->getMemModel());
    //args.append("-Wno-unused-value");

    if(projectOptions->getWarnAll().length())
        args.append(projectOptions->getWarnAll());
    if(projectOptions->get32bitDoubles().length())
        args.append(projectOptions->get32bitDoubles());
    if(projectOptions->getExceptions().length())
        args.append(projectOptions->getExceptions());
    if(projectOptions->getNoFcache().length())
        args.append(projectOptions->getNoFcache());
    if(projectOptions->getSimplePrintf().length())
        args.append(projectOptions->getSimplePrintf());

    if(projectOptions->getCompiler().indexOf("++") > -1)
        args.append("-fno-rtti");

    /* other compiler options */
    if(projectOptions->getCompOptions().length())
        args.append(projectOptions->getCompOptions());

    /* files */
    for(int n = 0; n < copts.length(); n++) {
        if(copts[n].length() > 0) {
            if(copts[n].indexOf(".c") > 0) // x.c
                args.append(copts[n]);
            else if(copts[n].indexOf(".o") > 0) // x.o
                args.append(copts[n]);
        }
    }

    /* libraries */
    if(projectOptions->getMathLib().length())
        args.append(projectOptions->getMathLib());
    if(projectOptions->getPthreadLib().length())
        args.append(projectOptions->getPthreadLib());
/*
    if(projectOptions->getCppStdLib().length())
        args.append(projectOptions->getCppStdLib());
    if(projectOptions->getCppSupLib().length())
        args.append(projectOptions->getCppSupLib());
*/
    /* other linker options */
    if(projectOptions->getLinkOptions().length())
        args.append(projectOptions->getLinkOptions());

    /* strip */
    if(projectOptions->getStripElf().length())
        args.append(projectOptions->getStripElf());

    qDebug() << args;
    return args;
}

int  MainWindow::runCompiler(QStringList copts)
{
    if(projectModel == NULL || projectFile.isNull()) {
        QMessageBox mbox(QMessageBox::Critical, "Error No Project",
            "Please select a tab and press F4 to set main project file.", QMessageBox::Ok);
        mbox.exec();
        return -1;
    }

    getApplicationSettings();
    if(checkCompilerInfo()) {
        return -1;
    }

    progress->setValue(0);

    QStringList args = getCompilerParameters(copts);

    btnConnected->setChecked(false);
    portListener->close(); // disconnect uart before use

    progress->setValue(5);

    checkAndSaveFiles();
    progress->setValue(10);

    QString compstr = shortFileName(aSideCompiler);
    showBuildStart(compstr,args);

    if(projectOptions->getCompiler().indexOf("++") > -1) {
        compstr = compstr.mid(0,compstr.lastIndexOf("-")+1);
        compstr+="c++";
    }

    QProcess proc(this);

    proc.setWorkingDirectory(sourcePath(projectFile));
    proc.start(compstr,args);

    progress->setValue(15);

    if(checkBuildStart(&proc, compstr))
        return -1;

    progress->setValue(60);

    QString result = proc.readAll();
    result += proc.readAllStandardOutput();
    result += proc.readAllStandardError();
    if(result.length()>0)
        compileStatus->appendPlainText(result);

    int exitCode = proc.exitCode();
    int exitStatus = proc.exitStatus();

    int rc = buildResult(exitStatus, exitCode, compstr, result);

    /*
     * Report program size
     * Use the projectFile instead of the current tab file
     */
    QString srcpath = sourcePath(projectFile);
    QFile aout(srcpath+"a.out");
    QString ssize = QString("Compiled %1K Bytes").arg(round(aout.size()/1000));
    programSize->setText(ssize);

    progress->setValue(100);
    return rc;
}

QStringList MainWindow::getLoaderParameters(QString copts)
{
    // use the projectFile instead of the current tab file
    QString srcpath = sourcePath(projectFile);

    portName = cbPort->itemText(cbPort->currentIndex());    // TODO should be itemToolTip
    boardName = cbBoard->itemText(cbBoard->currentIndex());

    QStringList args;
    args.append(tr("-b"));
    args.append(boardName);
    args.append(tr("-p"));
    args.append(portName);
    args.append(tr("-I"));
    args.append(aSideIncludes);
    args.append(srcpath+"a.out");
    QStringList olist = copts.split(" ");
    for (int n = 0; n < olist.length(); n++)
        args.append(olist[n]);

    qDebug() << args;
    return args;
}

int  MainWindow::runLoader(QString copts)
{
    if(projectModel == NULL || projectFile.isNull()) {
        QMessageBox mbox(QMessageBox::Critical, "Error No Project",
            "Please select a tab and press F4 to set main project file.", QMessageBox::Ok);
        mbox.exec();
        return -1;
    }

    getApplicationSettings();

    progress->setValue(0);

    QStringList args = getLoaderParameters(copts);

    btnConnected->setChecked(false);
    portListener->close(); // disconnect uart before use

    progress->setValue(5);

    showBuildStart(aSideLoader,args);

    QProcess proc(this);

    status->setText(status->text()+tr(" Loading ... "));

    proc.setWorkingDirectory(aSideCompilerPath);
    proc.start(aSideLoader,args);

    if(checkBuildStart(&proc, aSideLoader)) return -1;

    progress->setValue(60);

    QString result = proc.readAll();
    result += proc.readAllStandardOutput();
    result += proc.readAllStandardError();
    if(result.length()>0)
        compileStatus->appendPlainText(result);
    //termEditor->setPlainText(result);

    int exitCode = proc.exitCode();
    int exitStatus = proc.exitStatus();
    int rc = buildResult(exitStatus, exitCode, "", result);
    progress->setValue(100);
    status->setText(status->text()+tr(" done."));

    return rc;
}

int  MainWindow::checkBuildStart(QProcess *proc, QString progName)
{
    QMessageBox mbox;
    mbox.setStandardButtons(QMessageBox::Ok);
    qDebug() << QDir::currentPath();
    if(!proc->waitForStarted()) {
        progress->setValue(100);
        mbox.setInformativeText(progName+tr(" Could not start."));
        mbox.exec();
        return -1;
    }
    progress->setValue(50);
    if(!proc->waitForFinished()) {
        progress->setValue(100);
        mbox.setInformativeText(progName+tr(" Error waiting for program to finish."));
        mbox.exec();
        return -1;
    }
    return 0;
}

void MainWindow::showBuildStart(QString progName, QStringList args)
{
    QString argstr = "";
    for(int n = 0; n < args.length(); n++)
        argstr += " "+args[n];
    compileStatus->appendPlainText(progName+argstr);
}

int  MainWindow::buildResult(int exitStatus, int exitCode, QString progName, QString result)
{
    QMessageBox mbox;
    mbox.setStandardButtons(QMessageBox::Ok);
    mbox.setInformativeText(result);

    if(exitStatus == QProcess::CrashExit)
    {
        progress->setValue(100);
        status->setText(tr("Compiler Crashed"));
        mbox.setText(tr("Compiler Crashed"));
        mbox.exec();
    }
    else if(result.toLower().indexOf("error") > -1)
    { // just in case we get an error without exitCode
        progress->setValue(100);
        status->setText(progName+tr(" Error."));
    }
    else if(exitCode != 0)
    {
        progress->setValue(100);
        status->setText(progName+tr(" Unknown Error: ")+QString("%1").arg(exitCode));
    }
    else if(result.toLower().indexOf("warning") > -1)
    {
        status->setText(progName+tr(" Compiled OK with Warning(s)."));
        return 0;
    }
    else
    {
        status->setText(status->text()+" "+progName.mid(progName.lastIndexOf(aSideSeparator)+1));
        return 0;
    }
    return -1;
}

void MainWindow::compilerError(QProcess::ProcessError error)
{
    qDebug() << error;
}

void MainWindow::compilerFinished(int exitCode, QProcess::ExitStatus status)
{
    qDebug() << exitCode << status;
}

void MainWindow::closeTab(int index)
{
    fileChangeDisable = true;
    editors->at(index)->setPlainText("");
    editors->remove(index);
    if(editorTabs->count() == 1)
        newFile();
    editorTabs->removeTab(index);
    fileChangeDisable = false;
}

void MainWindow::changeTab(int index)
{
    if(editorTabs->count() == 1)
        setProject();
}

void MainWindow::addToolButton(QToolBar *bar, QToolButton *btn, QString imgfile)
{
    const QSize buttonSize(24, 24);
    btn->setIcon(QIcon(QPixmap(imgfile.toAscii())));
    btn->setMinimumSize(buttonSize);
    btn->setMaximumSize(buttonSize);
    btn->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    bar->addWidget(btn);
}

void MainWindow::setupProjectTools(QSplitter *vsplit)
{
    /* container for project, etc... */
    leftSplit = new QSplitter(this);
    leftSplit->setMinimumHeight(APPWINDOW_MIN_HEIGHT-60);
    leftSplit->setOrientation(Qt::Vertical);
    vsplit->addWidget(leftSplit);

    /* project tree */
    projectTree = new QTreeView(this);
    projectTree->setMinimumWidth(PROJECT_WIDTH);
    projectTree->setMaximumWidth(PROJECT_WIDTH);
    projectTree->setToolTip(tr("Current Project"));
    connect(projectTree,SIGNAL(pressed(QModelIndex)),this,SLOT(projectTreeClicked(QModelIndex)));
    leftSplit->addWidget(projectTree);

    // projectMenu is popup for projectTree
    projectMenu = new QMenu(QString("Project Menu"));
    projectMenu->addAction(tr("Add File"), this,SLOT(addProjectFile()));
    projectMenu->addAction(tr("Delete File"), this,SLOT(deleteProjectFile()));
    //projectMenu->addAction(tr("Show File"), this,SLOT(showProjectFile()));

    projectOptions = new ProjectOptions(this);
    projectOptions->setMinimumWidth(PROJECT_WIDTH);
    projectOptions->setMaximumWidth(PROJECT_WIDTH);
    projectOptions->setToolTip(tr("Project Options"));
    leftSplit->addWidget(projectOptions);

    QList<int> lsizes = leftSplit->sizes();
    lsizes[0] = leftSplit->height()*2/10;
    lsizes[1] = leftSplit->height()*8/10;
    leftSplit->setSizes(lsizes);

    rightSplit = new QSplitter(this);
    rightSplit->setMinimumHeight(APPWINDOW_MIN_HEIGHT-50);
    rightSplit->setOrientation(Qt::Vertical);
    vsplit->addWidget(rightSplit);

    /* project editors */
    editors = new QVector<QPlainTextEdit*>();

    /* project editor tabs */
    editorTabs = new QTabWidget(this);
    editorTabs->setTabsClosable(true);
    editorTabs->setMinimumWidth(EDITOR_MIN_WIDTH);
    connect(editorTabs,SIGNAL(tabCloseRequested(int)),this,SLOT(closeTab(int)));
    connect(editorTabs,SIGNAL(currentChanged(int)),this,SLOT(changeTab(int)));
    rightSplit->addWidget(editorTabs);

    compileStatus = new QPlainTextEdit(this);
    compileStatus->setLineWrapMode(QPlainTextEdit::NoWrap);
    connect(compileStatus,SIGNAL(selectionChanged()),this,SLOT(compileStatusClicked()));
    rightSplit->addWidget(compileStatus);

    QList<int> rsizes = rightSplit->sizes();
    rsizes[0] = rightSplit->height()*3/4;
    rsizes[1] = rightSplit->height()*1/4;
    rightSplit->setSizes(rsizes);
}

/*
 * Find error or warning in a file
 */
void MainWindow::compileStatusClicked(void)
{
    int n = 0;
    QTextCursor cur = compileStatus->textCursor();
    cur.movePosition(QTextCursor::StartOfLine,QTextCursor::MoveAnchor);
    cur.movePosition(QTextCursor::EndOfLine,QTextCursor::KeepAnchor);
    compileStatus->setTextCursor(cur);
    QString line = cur.selectedText();
    QStringList list = line.split(":");
    if(list.count() < 2)
        return;

    QString file = list[0];
    /* open file in tab if not there already */
    for(n = 0; n < editorTabs->count();n++) {
        if(editorTabs->tabText(n).contains(file)) {
            editorTabs->setCurrentIndex(n);
            break;
        }
    }
    if(n > editorTabs->count()-1) {
        file = sourcePath(projectFile)+list[0];
        openFileName(file);
    }

    QPlainTextEdit *editor = editors->at(editorTabs->currentIndex());
    if(editor != NULL)
    {
        n = QString(list[1]).toInt();
        QTextCursor c = editor->textCursor();
        c.movePosition(QTextCursor::Start);
        if(n > 0) {
            c.movePosition(QTextCursor::Down,QTextCursor::MoveAnchor,n-1);
            c.movePosition(QTextCursor::StartOfLine);
            c.movePosition(QTextCursor::EndOfLine,QTextCursor::KeepAnchor,1);
        }
        editor->setTextCursor(c);
        editor->setFocus();
    }
}

void MainWindow::projectTreeClicked(QModelIndex index)
{
    if(projectModel == NULL)
        return;

    projectIndex = index;
    Qt::MouseButtons buttons = QApplication::mouseButtons();
    if(buttons == Qt::RightButton) {
        projectMenu->popup(QCursor::pos());
    }
    else {
        showProjectFile();
    }
}

void MainWindow::addProjectFile()
{
    QFileDialog dialog(this);
    dialog.setDirectory(sourcePath(projectFile));

#ifdef OPEN_MULTIPLE_FILES
    // this is on the wish list and not finished yet
    dialog.setFileMode(QFileDialog::ExistingFiles);
    dialog.getOpenFileNames(this,
        tr("Add File"), "", "Program Source Files (*.c | *.cpp | *.h | *.cogc | *.spin | *.*)");
#else

    QString fileName = dialog.getOpenFileName(this,
        tr("Add File"), "", "Program Source Files (*.c | *.cpp | *.h | *.cogc | *.spin | *.*)");

    QString ext = fileName.mid(fileName.lastIndexOf("."));
    if(ext.length()) {
        ext = ext.toLower();
        if(ext == ".cog") {
            // don't copy .cog files
        }
        else if(ext == ".dat") {
            // don't copy .dat files
        }
        else if(ext == ".o") {
            // don't copy .o files
        }
        else if(ext == ".out") {
            // don't copy .out files
        }
        else if(ext == ".side") {
            // don't copy .side files
        }
        else {
            QFile copy(sourcePath(projectFile)+this->shortFileName(fileName));
            QString copystr = "";
            QFile reader(fileName);
            if(reader.open(QFile::ReadOnly | QFile::Text)) {
                copystr = reader.readAll();
                reader.close();
            }
            if(copy.open(QFile::WriteOnly | QFile::Text)) {
                copy.write(copystr.toAscii());
                copy.close();
            }
        }
    }

    QString projstr = "";
    QStringList list;
    QString mainFile;

    QFile file(projectFile);
    if(file.exists()) {
        if(file.open(QFile::ReadOnly | QFile::Text)) {
            projstr = file.readAll();
            file.close();
        }
        list = projstr.split("\n");
        mainFile = list[0];
        projstr = "";
        for(int n = 0; n < list.length(); n++) {
            QString arg = list[n];
            if(!arg.length())
                continue;
            if(arg.at(0) == '>')
                continue;
            projstr += arg + "\n";
        }
        projstr += this->shortFileName(fileName) + "\n";
        list.clear();
        list = projectOptions->getOptions();
        foreach(QString arg, list) {
            projstr += ">"+arg+"\n";
        }

        if(file.open(QFile::WriteOnly | QFile::Text)) {
            file.write(projstr.toAscii());
            file.close();
        }
    }
    updateProjectTree(sourcePath(projectFile)+mainFile,"");
#endif
}

void MainWindow::saveProjectOptions()
{
    QString projstr = "";
    QStringList list;

    if(projectFile.length() > 0)
        setWindowTitle(QString(ASideGuiKey)+" "+projectFile);

    QFile file(projectFile);
    if(file.exists()) {
        if(file.open(QFile::ReadOnly | QFile::Text)) {
            projstr = file.readAll();
            file.close();
        }
        list = projstr.split("\n");
        projstr = "";
        for(int n = 0; n < list.length(); n++) {
            QString arg = list[n];
            if(!arg.length())
                continue;
            if(arg.at(0) == '>')
                continue;
            projstr += arg + "\n";
        }
        list.clear();
        list = projectOptions->getOptions();
        foreach(QString arg, list) {
            if(arg.at(0) == '>')
                projstr += arg+"\n";
            else
                projstr += ">"+arg+"\n";
        }

        if(file.open(QFile::WriteOnly | QFile::Text)) {
            file.write(projstr.toAscii());
            file.close();
        }
    }
}


void MainWindow::deleteProjectFile()
{
    QString projstr = "";
    QString fileName = "";
    QStringList list;

    QVariant vs = projectModel->data(projectIndex, Qt::DisplayRole);
    if(vs.canConvert(QVariant::String))
    {
        fileName = vs.toString();
    }

    QString mainFile;

    QFile file(projectFile);
    if(file.exists()) {
        if(file.open(QFile::ReadOnly | QFile::Text)) {
            projstr = file.readAll();
            file.close();
        }
        list = projstr.split("\n");
        mainFile = list[0];
        projstr = "";
        for(int n = 0; n < list.length(); n++) {
            QString arg = list[n];
            if(!arg.length())
                continue;
            if(arg.at(0) == '>')
                continue;
            if(!n || fileName != arg)
                projstr += arg + "\n";
        }
        list.clear();
        list = projectOptions->getOptions();
        foreach(QString arg, list) {
            projstr += ">"+arg+"\n";
        }
        if(file.open(QFile::WriteOnly | QFile::Text)) {
            file.write(projstr.toAscii());
            file.close();
        }
    }
    updateProjectTree(sourcePath(projectFile)+mainFile,"");
}

void MainWindow::showProjectFile()
{
    QString fileName;

    QVariant vs = projectModel->data(projectIndex, Qt::DisplayRole);
    if(vs.canConvert(QVariant::String))
    {
        fileName = vs.toString();
        openFileName(sourcePath(projectFile)+fileName);
    }
}

void MainWindow::updateProjectTree(QString fileName, QString text)
{
    QString projName = this->shortFileName(fileName);
    projName = projName.mid(0,projName.lastIndexOf("."));
    projName += ".side";

    basicPath = sourcePath(fileName);
    projectFile = basicPath+projName;
    setWindowTitle(QString(ASideGuiKey)+" "+projectFile);

    if(projectModel != NULL) delete projectModel;
    projectModel = new CBuildTree(projName, this);

    QFile file(projectFile);
    if(!file.exists()) {
        if (file.open(QFile::WriteOnly | QFile::Text)) {
            file.write(this->shortFileName(fileName).toAscii());
            projectModel->addRootItem(this->shortFileName(fileName));
            file.close();
        }
    }
    else {
        QString s = "";
        if (file.open(QFile::ReadOnly | QFile::Text)) {
            s = file.readAll();
            file.close();
        }
        QStringList list = s.split("\n");
        projectOptions->clearOptions();
        foreach(QString arg, list) {
            if(!arg.length())
                continue;
            if(arg.at(0) != '>')
                projectModel->addRootItem(arg);
            else
                projectOptions->setOptions(arg);
        }
    }

    projectTree->setWindowTitle(projName);
    projectTree->setModel(projectModel);
    projectTree->hide();
    projectTree->show();
}

void MainWindow::updateReferenceTree(QString fileName, QString text)
{
}

void MainWindow::referenceTreeClicked(QModelIndex index)
{
}

void MainWindow::setEditorTab(int num, QString shortName, QString fileName, QString text)
{
    QPlainTextEdit *editor = editors->at(num);
    fileChangeDisable = true;
    editor->setPlainText(text);
    fileChangeDisable = false;
    editorTabs->setTabText(num,shortName);
    editorTabs->setTabToolTip(num,fileName);
    editorTabs->setCurrentIndex(num);
}

void MainWindow::enumeratePorts()
{
    if(cbPort != NULL) cbPort->clear();
    QList<QextPortInfo> ports = QextSerialEnumerator::getPorts();
    QStringList stringlist;
    QString name;
    stringlist << "List of ports:";
    for (int i = 0; i < ports.size(); i++) {
        stringlist << "port name:" << ports.at(i).portName;
        stringlist << "friendly name:" << ports.at(i).friendName;
        stringlist << "physical name:" << ports.at(i).physName;
        stringlist << "enumerator name:" << ports.at(i).enumName;
        stringlist << "vendor ID:" << QString::number(ports.at(i).vendorID, 16);
        stringlist << "product ID:" << QString::number(ports.at(i).productID, 16);
        stringlist << "===================================";
#if defined(Q_WS_WIN32)
        name = ports.at(i).portName;
        if(name.contains(QString("LPT"),Qt::CaseInsensitive) == false)
            cbPort->addItem(name);
#elif defined(Q_WS_MAC)
        name = ports.at(i).portName;
        if(name.indexOf("usbserial") > -1)
            cbPort->addItem(name);
#else
        name = "/"+ports.at(i).physName;
        cbPort->addItem(name);
#endif
    }
}

void MainWindow::connectButton()
{
    if(btnConnected->isChecked()) {
        btnConnected->setDisabled(true);
        portListener->open();
        btnConnected->setDisabled(false);
#if COMPILE_STATUS_TERMINAL
#else
        term->show();
#endif
    }
    else {
        portListener->close();
    }
}

QString MainWindow::shortFileName(QString fileName)
{
    QString rets;
    if(fileName.indexOf('/') > -1)
        rets = fileName.mid(fileName.lastIndexOf('/')+1);
    else if(fileName.indexOf('\\') > -1)
        rets = fileName.mid(fileName.lastIndexOf('\\')+1);
    else
        rets = fileName.mid(fileName.lastIndexOf(aSideSeparator)+1);
    return rets;
}

void MainWindow::initBoardTypes()
{
    cbBoard->clear();

    QFile file;
    if(file.exists(aSideCfgFile))
    {
        /* load boards in case there were changes */
        aSideConfig->loadBoards(aSideCfgFile);
    }

    /* get board types */
    QStringList boards = aSideConfig->getBoardNames();
    for(int n = 0; n < boards.count(); n++)
        cbBoard->addItem(boards.at(n));

    QVariant lastboardv = settings->value(lastBoardNameKey);
    /* read last board/port to make user happy */
    if(lastboardv.canConvert(QVariant::String))
        boardName = lastboardv.toString();

    /* setup the first board displayed in the combo box */
    if(cbBoard->count() > 0) {
        int ndx = 0;
        if(boardName.length() != 0) {
            for(int n = cbBoard->count()-1; n > -1; n--)
                if(cbBoard->itemText(n) == boardName)
                {
                    ndx = n;
                    break;
                }
        }
        setCurrentBoard(ndx);
    }
}

void MainWindow::setupEditor()
{
    QFont font;
    font.setFamily("Courier");
    font.setFixedPitch(true);
    font.setPointSize(10);

    QPlainTextEdit *editor = new QPlainTextEdit;
    editor->setTabStopWidth(40);
    editor->setFont(font);
    editor->setLineWrapMode(QPlainTextEdit::NoWrap);
    connect(editor,SIGNAL(textChanged()),this,SLOT(fileChanged()));
    highlighter = new Highlighter(editor->document());
    editors->append(editor);
}

void MainWindow::setupFileMenu()
{
    QMenu *fileMenu = new QMenu(tr("&File"), this);
    menuBar()->addMenu(fileMenu);

    fileMenu->addAction(QIcon(":/images/newfile.png"), tr("&New"), this, SLOT(newFile()),
                        QKeySequence::New);

    fileMenu->addAction(QIcon(":/images/openfile.png"), tr("&Open"), this, SLOT(openFile()),
                        QKeySequence::Open);

    fileMenu->addAction(QIcon(":/images/savefile.png"), tr("&Save"), this, SLOT(saveFile()),
                        QKeySequence::Save);

    fileMenu->addAction(QIcon(":/images/saveasfile.png"), tr("Save &As"), this, SLOT(saveAsFile()),
                        QKeySequence::SaveAs);

    // recent project actions
    separatorAct = fileMenu->addSeparator();

    for (int i = 0; i < MaxRecentProjects; ++i) {
        recentProjectActs[i] = new QAction(this);
        recentProjectActs[i]->setVisible(false);
        connect(recentProjectActs[i], SIGNAL(triggered()),
                this, SLOT(openRecentProject()));
    }

    for (int i = 0; i < MaxRecentProjects; ++i)
        fileMenu->addAction(recentProjectActs[i]);

    updateRecentProjectActions();

    fileMenu->addSeparator();

    // fileMenu->addAction(QIcon(":/images/print.png"), tr("Print"), this, SLOT(printFile()), QKeySequence::Print);
    // fileMenu->addAction(QIcon(":/images/zip.png"), tr("Archive"), this, SLOT(zipFile()), 0);

    fileMenu->addAction(QIcon(":/images/exit.png"), tr("E&xit"), qApp, SLOT(quit()),
                        QKeySequence::Quit);

    QMenu *editMenu = new QMenu(tr("&Edit"), this);
    menuBar()->addMenu(editMenu);

    editMenu->addAction(tr("Copy"), this, SLOT(copyFromFile()), QKeySequence::Copy);
    editMenu->addAction(tr("Cut"), this, SLOT(cutFromFile()), QKeySequence::Cut);
    editMenu->addAction(tr("Paste"), this, SLOT(pasteToFile()), QKeySequence::Paste);
/*
    editMenu->addSeparator();
    editMenu->addAction(tr("Edit Command"), this, SLOT(editCommand()), Qt::CTRL + Qt::Key_Colon);
    editMenu->addAction(tr("System Command"), this, SLOT(systemCommand()), Qt::CTRL + Qt::Key_Semicolon);
*/
    editMenu->addSeparator();
    editMenu->addAction(QIcon(":/images/find.png"), tr("&Find"), this, SLOT(findInFile()), QKeySequence::Find);
    editMenu->addAction(tr("Find Next"), this, SLOT(findNextInFile()), QKeySequence::FindNext);
    //editMenu->addAction(tr("Find Previous"), this, SLOT(findPrevInFile()), QKeySequence::FindPrevious);

    editMenu->addSeparator();
    editMenu->addAction(QIcon(":/images/redo.png"), tr("&Redo"), this, SLOT(redoChange()), QKeySequence::Redo);
    editMenu->addAction(QIcon(":/images/undo.png"), tr("&Undo"), this, SLOT(undoChange()), QKeySequence::Undo);

    QMenu *projMenu = new QMenu(tr("&Project"), this);
    menuBar()->addMenu(projMenu);

    projMenu->addAction(QIcon(":/images/treeproject.png"), tr("Set Project"), this, SLOT(setProject()), Qt::Key_F4);
    projMenu->addAction(QIcon(":/images/properties.png"), tr("Properties"), this, SLOT(properties()), Qt::Key_F5);
    //projMenu->addAction(QIcon(":/images/hardware.png"), tr("Load Board Types"), this, SLOT(hardware()), Qt::Key_F6);

    QMenu *debugMenu = new QMenu(tr("&Debug"), this);
    menuBar()->addMenu(debugMenu);

    debugMenu->addAction(QIcon(":/images/debug.png"), tr("Debug"), this, SLOT(programDebug()), Qt::Key_F8);
    debugMenu->addAction(QIcon(":/images/build.png"), tr("Build"), this, SLOT(programBuild()), Qt::Key_F9);
    debugMenu->addAction(QIcon(":/images/run.png"), tr("Run"), this, SLOT(programRun()), Qt::Key_F10);
    debugMenu->addAction(QIcon(":/images/burnee.png"), tr("Burn"), this, SLOT(programBurnEE()), Qt::Key_F11);
}


void MainWindow::setupToolBars()
{
    fileToolBar = addToolBar(tr("File"));
    QToolButton *btnFileNew = new QToolButton(this);
    QToolButton *btnFileOpen = new QToolButton(this);
    QToolButton *btnFileSave = new QToolButton(this);
    QToolButton *btnFileSaveAs = new QToolButton(this);
    //QToolButton *btnFilePrint = new QToolButton(this);
    //QToolButton *btnFileZip = new QToolButton(this);

    addToolButton(fileToolBar, btnFileNew, QString(":/images/newfile.png"));
    addToolButton(fileToolBar, btnFileOpen, QString(":/images/openfile.png"));
    addToolButton(fileToolBar, btnFileSave, QString(":/images/savefile.png"));
    addToolButton(fileToolBar, btnFileSaveAs, QString(":/images/saveasfile.png"));
    //addToolButton(fileToolBar, btnFilePrint, QString(":/images/print.png"));
    //addToolButton(fileToolBar, btnFileZip, QString(":/images/zip.png"));

    connect(btnFileNew,SIGNAL(clicked()),this,SLOT(newFile()));
    connect(btnFileOpen,SIGNAL(clicked()),this,SLOT(openFile()));
    connect(btnFileSave,SIGNAL(clicked()),this,SLOT(saveFile()));
    connect(btnFileSaveAs,SIGNAL(clicked()),this,SLOT(saveAsFile()));
    //connect(btnFilePrint,SIGNAL(clicked()),this,SLOT(printFile()));
    //connect(btnFileZip,SIGNAL(clicked()),this,SLOT(zipFile()));

    btnFileNew->setToolTip(tr("New"));
    btnFileOpen->setToolTip(tr("Open"));
    btnFileSave->setToolTip(tr("Save"));
    btnFileSaveAs->setToolTip(tr("Save As"));
    //btnFilePrint->setToolTip(tr("Print"));
    //btnFileZip->setToolTip(tr("Archive"));

    propToolBar = addToolBar(tr("Properties"));

/*
    QToolButton *btnProjectBoard = new QToolButton(this);
    addToolButton(propToolBar, btnProjectBoard, QString(":/images/hardware.png"));
    connect(btnProjectBoard,SIGNAL(clicked()),this,SLOT(hardware()));
    btnProjectBoard->setToolTip(tr("Configuration"));
*/
    QToolButton *btnProjectProperties = new QToolButton(this);
    addToolButton(propToolBar, btnProjectProperties, QString(":/images/properties.png"));
    connect(btnProjectProperties,SIGNAL(clicked()),this,SLOT(properties()));
    btnProjectProperties->setToolTip(tr("Properties"));

    QToolButton *btnProjectApp = new QToolButton(this);
    addToolButton(propToolBar, btnProjectApp, QString(":/images/treeproject.png"));
    connect(btnProjectApp,SIGNAL(clicked()),this,SLOT(setProject()));
    btnProjectApp->setToolTip(tr("Set Project File"));

    debugToolBar = addToolBar(tr("Debug"));
    QToolButton *btnDebugDebugTerm = new QToolButton(this);
    QToolButton *btnDebugRun = new QToolButton(this);
    QToolButton *btnDebugBuild = new QToolButton(this);
    QToolButton *btnDebugBurnEEP = new QToolButton(this);

    addToolButton(debugToolBar, btnDebugBuild, QString(":/images/build.png"));
    addToolButton(debugToolBar, btnDebugBurnEEP, QString(":/images/burnee.png"));
    addToolButton(debugToolBar, btnDebugRun, QString(":/images/run.png"));
    addToolButton(debugToolBar, btnDebugDebugTerm, QString(":/images/debug.png"));

    connect(btnDebugBuild,SIGNAL(clicked()),this,SLOT(programBuild()));
    connect(btnDebugBurnEEP,SIGNAL(clicked()),this,SLOT(programBurnEE()));
    connect(btnDebugDebugTerm,SIGNAL(clicked()),this,SLOT(programDebug()));
    connect(btnDebugRun,SIGNAL(clicked()),this,SLOT(programRun()));

    btnDebugBuild->setToolTip(tr("Build"));
    btnDebugBurnEEP->setToolTip(tr("Burn EEPROM"));
    btnDebugDebugTerm->setToolTip(tr("Debug"));
    btnDebugRun->setToolTip(tr("Run"));

    ctrlToolBar = addToolBar(tr("Control"));
    ctrlToolBar->setLayoutDirection(Qt::RightToLeft);
    cbBoard = new QComboBox(this);
    cbPort = new QComboBox(this);
    cbBoard->setLayoutDirection(Qt::LeftToRight);
    cbPort->setLayoutDirection(Qt::LeftToRight);
    cbBoard->setToolTip(tr("Board Type Select"));
    cbPort->setToolTip(tr("Serial Port Select"));
    cbBoard->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    cbPort->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    connect(cbBoard,SIGNAL(currentIndexChanged(int)),this,SLOT(setCurrentBoard(int)));
    connect(cbPort,SIGNAL(currentIndexChanged(int)),this,SLOT(setCurrentPort(int)));

    btnConnected = new QToolButton(this);
    btnConnected->setToolTip(tr("Port Status"));
    btnConnected->setCheckable(true);
    connect(btnConnected,SIGNAL(clicked()),this,SLOT(connectButton()));

    QToolButton *btnPortScan = new QToolButton(this);
    btnPortScan->setToolTip(tr("Rescan Serial Ports"));
    connect(btnPortScan,SIGNAL(clicked()),this,SLOT(enumeratePorts()));

    QToolButton *btnLoadBoards = new QToolButton(this);
    btnLoadBoards->setToolTip(tr("Reload Board List"));
    connect(btnLoadBoards,SIGNAL(clicked()),this,SLOT(initBoardTypes()));

    addToolButton(ctrlToolBar, btnConnected, QString(":/images/connected2.png"));
    addToolButton(ctrlToolBar, btnPortScan, QString(":/images/refresh.png"));
    ctrlToolBar->addWidget(cbPort);
    addToolButton(ctrlToolBar, btnLoadBoards, QString(":/images/hardware.png"));
    ctrlToolBar->addWidget(cbBoard);
    ctrlToolBar->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Fixed);
}
