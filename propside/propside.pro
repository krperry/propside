# -------------------------------------------------
# Project created by QtCreator 2011-11-19T08:29:49
# -------------------------------------------------
# add spin compiler
# make builder handle complicated projects
# hook terminal to loader
QT += core gui
TARGET = SimpleIDE
TEMPLATE = app
DEFINES += QEXTSERIALPORT_LIB

# EVENT_DRIVEN QEXTSERIALPORT is no longer used.
#
# GDBENABLE is not ready, and will only be used for development testing
# DEFINES += GDBENABLE
# These define the version number in Menu->About
DEFINES += IDEVERSION=0
DEFINES += MINVERSION=7
DEFINES += FIXVERSION=2

SOURCES += main.cpp \
    editor.cpp \
    ctags.cpp \
    mainwindow.cpp \
    treemodel.cpp \
    treeitem.cpp \
    terminal.cpp \
    properties.cpp \
    newproject.cpp \
    PortListener.cpp \
    highlighter.cpp \
    hardware.cpp \
    help.cpp \
    console.cpp \
    asideconfig.cpp \
    asideboard.cpp \
    cbuildtree.cpp \
    projectoptions.cpp \
    replacedialog.cpp \
    aboutdialog.cpp \
    gdb.cpp \
    loader.cpp \
    projecttree.cpp \
    qextserialport.cpp \
    qextserialenumerator.cpp

HEADERS += mainwindow.h \
    editor.h \
    ctags.h \
    highlighter.h \
    treemodel.h \
    treeitem.h \
    PortListener.h \
    terminal.h \
    properties.h \
    newproject.h \
    console.h \
    hardware.h \
    help.h \
    asideboard.h \
    asideconfig.h \
    cbuildtree.h \
    projectoptions.h \
    Sleeper.h \
    replacedialog.h \
    aboutdialog.h \
    gdb.h \
    loader.h \
    projecttree.h \
    qextserialport.h \
    qextserialenumerator.h

FORMS += hardware.ui \
    project.ui \
    TermPrefs.ui

RESOURCES += resources.qrc

unix:SOURCES += qextserialport_unix.cpp
unix:!macx {
    # dont use EVENT_DRIVEN for linux to be consistent with MAC. also causes output skips.
    SOURCES += qextserialenumerator_unix.cpp
}
macx { 
    # dont use EVENT_DRIVEN for mac. must open terminal before load because mac would reset boards otherwise.
    SOURCES += qextserialenumerator_osx.cpp
    LIBS += -framework \
        IOKit \
        -framework \
        CoreFoundation
}
win32 { 
    RC_FILE = myapp.rc
    
    # don't use EVENT_DRIVEN for windows. causes crashes.
    SOURCES += qextserialport_win.cpp
    SOURCES += qextserialenumerator_win.cpp
    DEFINES += WINVER=0x0501 # needed for mingw to pull in appropriate dbt business...probably a better way to do this
    LIBS += -lsetupapi
}
