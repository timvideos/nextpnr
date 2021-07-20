
/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2018  Miodrag Milanovic <micko@yosyshq.com>
 *  Copyright (C) 2018  Serge Bazanski <q3k@q3k.org>
 *
 *  Permission to use, copy, modify, and/or distribute this software for any
 *  purpose with or without fee is hereby granted, provided that the above
 *  copyright notice and this permission notice appear in all copies.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 *  WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 *  ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 *  WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 *  ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 *  OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 */

#include "application.h"
#include <QMessageBox>
#include <QOpenGLContext>
#include <QSurfaceFormat>
#include <QTextStream>
#include <exception>
#include "log.h"

NEXTPNR_NAMESPACE_BEGIN

#ifdef _WIN32
#include <windows.h>
BOOL WINAPI WinHandler(DWORD dwCtrlType)
{
    if (dwCtrlType == CTRL_C_EVENT)
        qApp->quit();
    return TRUE;
}
#endif

Application::Application(int &argc, char **argv, bool noantialiasing) : QApplication(argc, argv)
{
    QSurfaceFormat fmt;
    if (!noantialiasing)
        fmt.setSamples(10);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    // macOS is very picky about this version matching
    // the version of openGL  used in ImGuiRenderer
    fmt.setMajorVersion(3);
    fmt.setMinorVersion(2);
    QSurfaceFormat::setDefaultFormat(fmt);

    QOpenGLContext glContext;
    fmt = glContext.format();
    if (fmt.majorVersion() < 3) {
        printf("Could not get OpenGL 3.0 context. Aborting.\n");
        log_abort();
    }
    if (fmt.minorVersion() < 2) {
        printf("Could not get OpenGL 3.2 context - trying anyway...\n ");
    }

#ifdef _WIN32
    SetConsoleCtrlHandler((PHANDLER_ROUTINE)WinHandler, TRUE);
#endif
}

bool Application::notify(QObject *receiver, QEvent *event)
{
    bool retVal = true;
    try {
        retVal = QApplication::notify(receiver, event);
    } catch (const assertion_failure &ex) {
        QString msg;
        QTextStream out(&msg);
        out << ex.filename.c_str() << " at " << ex.line << "\n";
        out << ex.msg.c_str();
        QMessageBox::critical(0, "Error", msg);
    } catch (...) {
        QMessageBox::critical(0, "Error", "Fatal error !!!");
    }
    return retVal;
}

NEXTPNR_NAMESPACE_END
