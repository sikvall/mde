/**
 * @file version.h
 * @brief Application metadata, versioning, and build information.
 *
 * This header contains centralized application details (name, version,
 * build date, author, description) to allow easy updating in a single place.
 */

#ifndef MDE_VERSION_H
#define MDE_VERSION_H

#define MDE_APP_NAME        "mde"
#define MDE_APP_TITLE       "Markdown Editor"
#define MDE_VERSION_MAJOR   1
#define MDE_VERSION_MINOR   0
#define MDE_VERSION_PATCH   0
#define MDE_VERSION_STRING  "0.1.0"

#ifndef MDE_BUILD_DATE
#define MDE_BUILD_DATE      __DATE__
#endif

#ifndef MDE_BUILD_TIME
#define MDE_BUILD_TIME      __TIME__
#endif

#define MDE_AUTHOR          "Anders"
#define MDE_MAINTAINER      "Anders <anders@localhost>"
#define MDE_DESCRIPTION     "Full-screen text-mode Markdown editor"

#endif // MDE_VERSION_H
