/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Chimera Kang
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QByteArray>
#include <QHash>
#include <QJsonObject>
#include <QJsonValue>
#include <QObject>

#include <memory>

class App;
class IEventQueue;
class QLocalServer;
class QLocalSocket;

namespace deskflow::mcp {

class McpBridge;
class McpTools;

//! MCP (Model Context Protocol) server on a local socket, newline-delimited JSON-RPC 2.0.
/*!
Lives on the Qt main thread next to CoreIpcServer. Tool handlers that touch the
platform screen are forwarded to the core thread through McpBridge.
*/
class McpServer : public QObject
{
  Q_OBJECT

public:
  McpServer(IEventQueue *events, App *app, QObject *parent = nullptr);
  ~McpServer() override;

public Q_SLOTS:
  //! Start listening if Settings::Mcp::Enabled is true. Returns true when listening.
  //! Must run on the thread that owns this object (creates the QLocalServer there).
  bool start();

  //! Stop serving and detach from the core event queue; call before the queue is destroyed.
  void shutdown();

public:

  QString socketName() const
  {
    return m_socketName;
  }

private:
  void handleNewConnection();
  void handleReadyRead();
  void handleDisconnected();
  void processLine(QLocalSocket *socket, const QByteArray &line);
  QJsonObject handleRequest(const QJsonObject &request, bool &isNotification);
  QJsonObject callTool(const QJsonObject &params);
  static QJsonObject makeResult(const QJsonValue &id, const QJsonValue &result);
  static QJsonObject makeError(const QJsonValue &id, int code, const QString &message);
  static void send(QLocalSocket *socket, const QJsonObject &message);

  QLocalServer *m_server;
  QString m_socketName;
  std::unique_ptr<McpBridge> m_bridge;
  std::unique_ptr<McpTools> m_tools;
  QHash<QLocalSocket *, QByteArray> m_buffers;
};

} // namespace deskflow::mcp
