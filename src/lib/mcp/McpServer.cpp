/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Chimera Kang
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "McpServer.h"

#include "McpBridge.h"
#include "McpTools.h"
#include "base/Log.h"
#include "common/Constants.h"
#include "common/Settings.h"
#include "common/VersionInfo.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QLocalServer>
#include <QLocalSocket>

namespace deskflow::mcp {

namespace {
const auto kProtocolVersion = QStringLiteral("2025-06-18");
constexpr int kParseError = -32700;
constexpr int kInvalidRequest = -32600;
constexpr int kMethodNotFound = -32601;
constexpr int kInvalidParams = -32602;
} // namespace

McpServer::McpServer(IEventQueue *events, App *app, QObject *parent)
    : QObject(parent),
      m_server{nullptr},
      m_socketName(Settings::value(Settings::Mcp::SocketName).toString()),
      m_bridge(std::make_unique<McpBridge>(events)),
      m_tools(std::make_unique<McpTools>(m_bridge.get(), app, Settings::value(Settings::Mcp::TimeoutMs).toInt()))
{
}

McpServer::~McpServer() = default;

void McpServer::shutdown()
{
  if (m_server != nullptr && m_server->isListening())
    m_server->close();
  for (auto *socket : m_buffers.keys())
    socket->disconnectFromServer();
  m_buffers.clear();
  m_bridge->detach();
}

bool McpServer::start()
{
  if (!Settings::value(Settings::Mcp::Enabled).toBool()) {
    LOG_DEBUG("mcp server disabled (set mcp/enabled=true in settings to enable)");
    return false;
  }
  if (m_server == nullptr) {
    m_server = new QLocalServer(this);
    m_server->setSocketOptions(QLocalServer::UserAccessOption);
    connect(m_server, &QLocalServer::newConnection, this, &McpServer::handleNewConnection);
  }
  QLocalServer::removeServer(m_socketName);
  if (!m_server->listen(m_socketName)) {
    LOG_ERR("mcp server failed to listen on %s: %s", m_socketName.toUtf8().constData(),
            m_server->errorString().toUtf8().constData());
    return false;
  }
  LOG_INFO("mcp server listening on %s", m_server->fullServerName().toUtf8().constData());
  return true;
}

void McpServer::handleNewConnection()
{
  while (auto *socket = m_server->nextPendingConnection()) {
    LOG_DEBUG("mcp server got new connection");
    m_buffers.insert(socket, QByteArray());
    connect(socket, &QLocalSocket::readyRead, this, &McpServer::handleReadyRead);
    connect(socket, &QLocalSocket::disconnected, this, &McpServer::handleDisconnected);
  }
}

void McpServer::handleDisconnected()
{
  auto *socket = qobject_cast<QLocalSocket *>(sender());
  LOG_DEBUG("mcp server client disconnected");
  m_buffers.remove(socket);
  socket->deleteLater();
}

void McpServer::handleReadyRead()
{
  auto *socket = qobject_cast<QLocalSocket *>(sender());
  auto &buffer = m_buffers[socket];
  buffer.append(socket->readAll());
  while (true) {
    const auto index = buffer.indexOf('\n');
    if (index < 0)
      break;
    const QByteArray line = buffer.left(index).trimmed();
    buffer.remove(0, index + 1);
    if (!line.isEmpty())
      processLine(socket, line);
  }
}

void McpServer::processLine(QLocalSocket *socket, const QByteArray &line)
{
  QJsonParseError parseError{};
  const auto doc = QJsonDocument::fromJson(line, &parseError);
  if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
    send(socket, makeError(QJsonValue(), kParseError, parseError.errorString()));
    return;
  }
  bool isNotification = false;
  const auto response = handleRequest(doc.object(), isNotification);
  if (!isNotification)
    send(socket, response);
}

QJsonObject McpServer::handleRequest(const QJsonObject &request, bool &isNotification)
{
  const auto id = request.value("id");
  const auto method = request.value("method").toString();
  const auto params = request.value("params").toObject();
  isNotification = !request.contains("id");

  if (method.isEmpty())
    return makeError(id, kInvalidRequest, QStringLiteral("missing method"));

  if (method.startsWith("notifications/")) {
    isNotification = true;
    return {};
  }

  if (method == "initialize") {
    auto requested = params.value("protocolVersion").toString(kProtocolVersion);
    return makeResult(
        id, QJsonObject{
                {"protocolVersion", requested.isEmpty() ? kProtocolVersion : requested},
                {"capabilities", QJsonObject{{"tools", QJsonObject{}}}},
                {"serverInfo", QJsonObject{{"name", QStringLiteral("%1-mcp").arg(kAppId)}, {"version", kVersion}}},
                {"instructions",
                 QStringLiteral("Tools act on the machine named %1. Coordinates are that machine's screen pixels.")
                     .arg(Settings::value(Settings::Core::ComputerName).toString())},
            }
    );
  }
  if (method == "ping")
    return makeResult(id, QJsonObject{});
  if (method == "tools/list")
    return makeResult(id, QJsonObject{{"tools", m_tools->list()}});
  if (method == "tools/call") {
    const auto name = params.value("name").toString();
    if (name.isEmpty())
      return makeError(id, kInvalidParams, QStringLiteral("tools/call needs params.name"));
    return makeResult(id, callTool(params));
  }
  return makeError(id, kMethodNotFound, QStringLiteral("unknown method \"%1\"").arg(method));
}

QJsonObject McpServer::callTool(const QJsonObject &params)
{
  const auto result = m_tools->call(params.value("name").toString(), params.value("arguments").toObject());
  QJsonObject out{{"content", result.content}};
  if (result.isError)
    out.insert("isError", true);
  return out;
}

QJsonObject McpServer::makeResult(const QJsonValue &id, const QJsonValue &result)
{
  return QJsonObject{{"jsonrpc", "2.0"}, {"id", id}, {"result", result}};
}

QJsonObject McpServer::makeError(const QJsonValue &id, int code, const QString &message)
{
  return QJsonObject{{"jsonrpc", "2.0"}, {"id", id}, {"error", QJsonObject{{"code", code}, {"message", message}}}};
}

void McpServer::send(QLocalSocket *socket, const QJsonObject &message)
{
  socket->write(QJsonDocument(message).toJson(QJsonDocument::Compact));
  socket->write("\n");
  socket->flush();
}

} // namespace deskflow::mcp
