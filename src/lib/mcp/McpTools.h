/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Chimera Kang
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

#include <functional>
#include <map>

class App;

namespace deskflow::mcp {

class McpBridge;

//! Result of one MCP tool call, already in MCP "content" shape.
struct ToolResult
{
  QJsonArray content;
  bool isError = false;

  static ToolResult text(const QString &text);
  static ToolResult error(const QString &message);
  static ToolResult json(const QJsonObject &object);
  static ToolResult image(const QByteArray &png, const QString &caption);
};

//! Registry and implementation of the MCP tools exposed by deskflow-core.
class McpTools
{
public:
  using Handler = std::function<ToolResult(const QJsonObject &args)>;

  McpTools(McpBridge *bridge, App *app, int timeoutMs);

  //! MCP tools/list payload.
  QJsonArray list() const;

  //! Dispatch a tools/call. Unknown names yield an error result.
  ToolResult call(const QString &name, const QJsonObject &args) const;

private:
  struct Tool
  {
    QString description;
    QJsonObject inputSchema;
    Handler handler;
  };

  void add(const QString &name, const QString &description, const QJsonObject &schema, Handler handler);
  void registerLocalTools();
  void registerServerTools();

  //! Run \p fn on the core thread; returns an error result on timeout.
  ToolResult onCore(const std::function<ToolResult()> &fn) const;

  bool isServer() const;

  McpBridge *m_bridge;
  App *m_app;
  int m_timeoutMs;
  std::map<QString, Tool> m_tools;
};

} // namespace deskflow::mcp
