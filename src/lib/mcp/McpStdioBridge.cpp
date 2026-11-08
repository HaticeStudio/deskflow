/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Chimera Kang
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "McpStdioBridge.h"

#include <QCoreApplication>
#include <QLocalSocket>
#include <QSocketNotifier>
#include <QStringList>

#include <cstdio>
#include <unistd.h>

namespace deskflow::mcp {

namespace {

// Matches Settings::Mcp::SocketName for the default "deskflow" app id.
const char *kDefaultSocketName = "deskflow-mcp";

void writeLine(const QByteArray &line)
{
  std::fwrite(line.constData(), 1, static_cast<size_t>(line.size()), stdout);
  std::fputc('\n', stdout);
  std::fflush(stdout);
}

QByteArray errorReply(const QString &message)
{
  return QByteArrayLiteral("{\"jsonrpc\":\"2.0\",\"id\":null,\"error\":{\"code\":-32000,\"message\":\"") +
         message.toUtf8().replace('"', "'") + QByteArrayLiteral("\"}}");
}

} // namespace

int runStdioBridge(int argc, char **argv)
{
  QCoreApplication app(argc, argv);

  QString socketName = QString::fromLatin1(kDefaultSocketName);
  const QStringList args = QCoreApplication::arguments();
  for (int i = 1; i + 1 < args.size(); ++i) {
    if (args.at(i) == QLatin1String("--socket")) {
      socketName = args.at(i + 1);
    }
  }

  QLocalSocket socket;
  socket.connectToServer(socketName);
  if (!socket.waitForConnected(3000)) {
    writeLine(errorReply(QStringLiteral("no PeerDesk MCP server on socket \"%1\": %2 (is the app running with mcp/enabled=true?)")
                             .arg(socketName, socket.errorString())));
    return 1;
  }

  int exitCode = 0;
  QByteArray inBuf;
  QByteArray outBuf;

  // Socket -> stdout: whole lines only.
  QObject::connect(&socket, &QLocalSocket::readyRead, &app, [&] {
    outBuf.append(socket.readAll());
    qsizetype idx;
    while ((idx = outBuf.indexOf('\n')) >= 0) {
      const QByteArray line = outBuf.left(idx).trimmed();
      outBuf.remove(0, idx + 1);
      if (!line.isEmpty()) {
        writeLine(line);
      }
    }
  });

  QObject::connect(&socket, &QLocalSocket::disconnected, &app, [&] {
    writeLine(errorReply(QStringLiteral("PeerDesk MCP socket closed")));
    exitCode = 1;
    app.quit();
  });

  // stdin -> socket: forward complete lines; exit when the client closes stdin.
  QSocketNotifier stdinNotifier(STDIN_FILENO, QSocketNotifier::Read);
  QObject::connect(&stdinNotifier, &QSocketNotifier::activated, &app, [&] {
    char buf[65536];
    const ssize_t n = ::read(STDIN_FILENO, buf, sizeof(buf));
    if (n <= 0) {
      socket.disconnectFromServer();
      app.quit();
      return;
    }
    inBuf.append(buf, static_cast<qsizetype>(n));
    qsizetype idx;
    while ((idx = inBuf.indexOf('\n')) >= 0) {
      const QByteArray line = inBuf.left(idx).trimmed();
      inBuf.remove(0, idx + 1);
      if (!line.isEmpty()) {
        socket.write(line + '\n');
        socket.flush();
      }
    }
  });

  app.exec();
  socket.disconnectFromServer();
  return exitCode;
}

} // namespace deskflow::mcp
