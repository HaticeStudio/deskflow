/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Chimera Kang
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "McpTools.h"

#include "McpBridge.h"
#include "base/Event.h"
#include "base/EventTypes.h"
#include "base/IEventQueue.h"
#include "base/Log.h"
#include "common/Settings.h"
#include "deskflow/ClientApp.h"
#include "deskflow/Clipboard.h"
#include "deskflow/ClipboardTypes.h"
#include "deskflow/Computer.h"
#include "deskflow/IPlatformComputer.h"
#include "deskflow/KeyTypes.h"
#include "deskflow/MouseTypes.h"
#include "deskflow/ServerApp.h"
#include "server/PrimaryClient.h"
#include "server/Server.h"

#if defined(Q_OS_MAC) || defined(__APPLE__)
#include "OSXScreenshot.h"
#endif

#include <QJsonValue>

namespace deskflow::mcp {

// ---------------------------------------------------------------------------
// ToolResult helpers

ToolResult ToolResult::text(const QString &text)
{
  ToolResult r;
  r.content.append(QJsonObject{{"type", "text"}, {"text", text}});
  return r;
}

ToolResult ToolResult::error(const QString &message)
{
  ToolResult r = text(message);
  r.isError = true;
  return r;
}

ToolResult ToolResult::json(const QJsonObject &object)
{
  return text(QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact)));
}

ToolResult ToolResult::image(const QByteArray &png, const QString &caption)
{
  ToolResult r;
  r.content.append(
      QJsonObject{{"type", "image"}, {"data", QString::fromLatin1(png.toBase64())}, {"mimeType", "image/png"}}
  );
  r.content.append(QJsonObject{{"type", "text"}, {"text", caption}});
  return r;
}

// ---------------------------------------------------------------------------
// Schema helpers

namespace {

QJsonObject prop(const QString &type, const QString &description)
{
  return QJsonObject{{"type", type}, {"description", description}};
}

QJsonObject schema(const QJsonObject &properties, const QJsonArray &required = {})
{
  QJsonObject s{{"type", "object"}, {"properties", properties}};
  if (!required.isEmpty())
    s.insert("required", required);
  return s;
}

ButtonID buttonFromName(const QString &name)
{
  const auto n = name.toLower();
  if (n == "right")
    return kButtonRight;
  if (n == "middle")
    return kButtonMiddle;
  return kButtonLeft;
}

struct KeySpec
{
  KeyID id = kKeyNone;
  KeyModifierMask mask = 0;
};

KeyID namedKey(const QString &token)
{
  static const std::map<QString, KeyID> keys = {
      {"enter", kKeyReturn},       {"return", kKeyReturn},   {"tab", kKeyTab},
      {"esc", kKeyEscape},         {"escape", kKeyEscape},   {"space", static_cast<KeyID>(' ')},
      {"backspace", kKeyBackSpace}, {"delete", kKeyDelete},  {"del", kKeyDelete},
      {"up", kKeyUp},              {"down", kKeyDown},       {"left", kKeyLeft},
      {"right", kKeyRight},        {"home", kKeyHome},       {"end", kKeyEnd},
      {"pageup", kKeyPageUp},      {"pagedown", kKeyPageDown},
      {"f1", kKeyF1},   {"f2", kKeyF2},   {"f3", kKeyF3},   {"f4", kKeyF4},
      {"f5", kKeyF5},   {"f6", kKeyF6},   {"f7", kKeyF7},   {"f8", kKeyF8},
      {"f9", kKeyF9},   {"f10", kKeyF10}, {"f11", kKeyF11}, {"f12", kKeyF12},
  };
  const auto t = token.toLower();
  if (const auto it = keys.find(t); it != keys.end())
    return it->second;
  if (token.size() == 1)
    return static_cast<KeyID>(token.at(0).unicode());
  return kKeyNone;
}

//! Parse "cmd+shift+4" into modifier keys plus the main key.
bool parseCombo(const QString &combo, std::vector<KeyID> &modifierKeys, KeySpec &key, QString &error)
{
  const auto parts = combo.split('+', Qt::SkipEmptyParts);
  if (parts.isEmpty()) {
    error = QStringLiteral("empty key combo");
    return false;
  }
  for (int i = 0; i < parts.size(); ++i) {
    const auto p = parts.at(i).trimmed();
    const auto l = p.toLower();
    const bool last = (i == parts.size() - 1);
    if (l == "cmd" || l == "command" || l == "super" || l == "win") {
      modifierKeys.push_back(kKeySuper_L);
      key.mask |= KeyModifierSuper;
    } else if (l == "ctrl" || l == "control") {
      modifierKeys.push_back(kKeyControl_L);
      key.mask |= KeyModifierControl;
    } else if (l == "alt" || l == "option" || l == "opt") {
      modifierKeys.push_back(kKeyAlt_L);
      key.mask |= KeyModifierAlt;
    } else if (l == "shift") {
      modifierKeys.push_back(kKeyShift_L);
      key.mask |= KeyModifierShift;
    } else if (last) {
      key.id = namedKey(p);
      if (key.id == kKeyNone) {
        error = QStringLiteral("unknown key \"%1\"").arg(p);
        return false;
      }
    } else {
      error = QStringLiteral("unknown modifier \"%1\"").arg(p);
      return false;
    }
  }
  if (key.id == kKeyNone) {
    error = QStringLiteral("combo \"%1\" has no main key").arg(combo);
    return false;
  }
  return true;
}

//! Fake key buttons must be distinct per concurrently-held key; rotate in a private range.
KeyButton nextButton()
{
  static KeyButton counter = 0;
  counter = static_cast<KeyButton>((counter + 1) % 0x0F00);
  return static_cast<KeyButton>(0xF000 + counter);
}

void tap(IPlatformComputer *pc, KeyID id, KeyModifierMask mask)
{
  const auto button = nextButton();
  pc->fakeKeyDown(id, mask, button, std::string());
  pc->fakeKeyUp(button);
}

} // namespace

// ---------------------------------------------------------------------------

McpTools::McpTools(McpBridge *bridge, App *app, int timeoutMs) : m_bridge(bridge), m_app(app), m_timeoutMs(timeoutMs)
{
  registerLocalTools();
  if (isServer())
    registerServerTools();
}

bool McpTools::isServer() const
{
  return dynamic_cast<ServerApp *>(m_app) != nullptr;
}

void McpTools::add(const QString &name, const QString &description, const QJsonObject &schema, Handler handler)
{
  m_tools[name] = Tool{description, schema, std::move(handler)};
}

QJsonArray McpTools::list() const
{
  QJsonArray out;
  for (const auto &[name, tool] : m_tools) {
    out.append(QJsonObject{{"name", name}, {"description", tool.description}, {"inputSchema", tool.inputSchema}});
  }
  return out;
}

ToolResult McpTools::call(const QString &name, const QJsonObject &args) const
{
  const auto it = m_tools.find(name);
  if (it == m_tools.end())
    return ToolResult::error(QStringLiteral("unknown tool \"%1\"").arg(name));
  LOG_INFO("mcp tool call: %s", name.toUtf8().constData());
  return it->second.handler(args);
}

ToolResult McpTools::onCore(const std::function<ToolResult()> &fn) const
{
  ToolResult result;
  const bool ran = m_bridge->runOnCore([&] { result = fn(); }, m_timeoutMs);
  if (!ran)
    return ToolResult::error(QStringLiteral("core did not respond within %1 ms (is deskflow-core running?)").arg(m_timeoutMs));
  return result;
}

// ---------------------------------------------------------------------------
// Local machine tools

namespace {

//! Platform screen of this machine, valid only on the core thread.
IPlatformComputer *localPlatform(App *app, QString &error)
{
  deskflow::Computer *computer = nullptr;
  if (auto *server = dynamic_cast<ServerApp *>(app); server != nullptr) {
    if (auto *primary = server->getPrimaryClient(); primary != nullptr)
      computer = primary->getComputer();
  } else if (auto *client = dynamic_cast<ClientApp *>(app); client != nullptr) {
    computer = client->getComputer();
  }
  if (computer == nullptr) {
    error = QStringLiteral("screen not available yet (core still starting or stopped)");
    return nullptr;
  }
  return computer->getPlatformComputer();
}

} // namespace

void McpTools::registerLocalTools()
{
  add("screen_info", "Name, role, screen shape, cursor position and (on the server) connected computers of this machine.",
      schema({}), [this](const QJsonObject &) {
        return onCore([this]() -> ToolResult {
          QJsonObject info;
          info.insert("name", Settings::value(Settings::Core::ComputerName).toString());
          info.insert("mode", isServer() ? "server" : "client");
          QString err;
          if (auto *pc = localPlatform(m_app, err); pc != nullptr) {
            int32_t x = 0, y = 0, w = 0, h = 0, cx = 0, cy = 0;
            pc->getShape(x, y, w, h);
            pc->getCursorPos(cx, cy);
            info.insert("shape", QJsonObject{{"x", x}, {"y", y}, {"width", w}, {"height", h}});
            info.insert("cursor", QJsonObject{{"x", cx}, {"y", cy}});
          } else {
            info.insert("warning", err);
          }
          if (auto *server = dynamic_cast<ServerApp *>(m_app); server != nullptr && server->getServerPtr() != nullptr) {
            std::vector<std::string> clients;
            server->getServerPtr()->getClients(clients);
            QJsonArray arr;
            for (const auto &c : clients)
              arr.append(QString::fromStdString(c));
            info.insert("connected", arr);
          }
#if defined(__APPLE__)
          info.insert("displays", displayCount());
#endif
          return ToolResult::json(info);
        });
      });

#if defined(__APPLE__)
  add("screenshot", "Capture a display of this machine as PNG. Needs the macOS Screen Recording permission.",
      schema({{"display", prop("integer", "Display index, 0 = main (default 0)")},
              {"scale", prop("number", "Scale factor 0.05..1.0 (default 0.5)")}}),
      [](const QJsonObject &args) {
        const int display = args.value("display").toInt(0);
        const double scale = args.value("scale").toDouble(0.5);
        const auto shot = captureDisplayPng(display, scale);
        if (!shot.error.isEmpty())
          return ToolResult::error(shot.error);
        return ToolResult::image(
            shot.png, QStringLiteral("display %1, %2x%3 px, scale %4").arg(display).arg(shot.width).arg(shot.height).arg(scale)
        );
      });
#endif

  add("mouse_move", "Move the mouse cursor to absolute screen coordinates on this machine.",
      schema({{"x", prop("integer", "X in screen pixels")}, {"y", prop("integer", "Y in screen pixels")}},
             {"x", "y"}),
      [this](const QJsonObject &args) {
        const int x = args.value("x").toInt(), y = args.value("y").toInt();
        return onCore([&]() -> ToolResult {
          QString err;
          auto *pc = localPlatform(m_app, err);
          if (pc == nullptr)
            return ToolResult::error(err);
          pc->fakeMouseMove(x, y);
          return ToolResult::text(QStringLiteral("moved to %1,%2").arg(x).arg(y));
        });
      });

  add("mouse_click", "Click a mouse button, optionally moving to x,y first.",
      schema({{"x", prop("integer", "Optional X")},
              {"y", prop("integer", "Optional Y")},
              {"button", prop("string", "left (default), right or middle")},
              {"double", prop("boolean", "Double click (default false)")}}),
      [this](const QJsonObject &args) {
        const bool hasPos = args.contains("x") && args.contains("y");
        const int x = args.value("x").toInt(), y = args.value("y").toInt();
        const auto button = buttonFromName(args.value("button").toString("left"));
        const int clicks = args.value("double").toBool(false) ? 2 : 1;
        return onCore([&]() -> ToolResult {
          QString err;
          auto *pc = localPlatform(m_app, err);
          if (pc == nullptr)
            return ToolResult::error(err);
          if (hasPos)
            pc->fakeMouseMove(x, y);
          for (int i = 0; i < clicks; ++i) {
            pc->fakeMouseButton(button, true);
            pc->fakeMouseButton(button, false);
          }
          return ToolResult::text(QStringLiteral("clicked %1 x%2").arg(args.value("button").toString("left")).arg(clicks));
        });
      });

  add("mouse_scroll", "Scroll the mouse wheel. 120 units = one notch; positive dy scrolls up.",
      schema({{"dx", prop("integer", "Horizontal delta (default 0)")}, {"dy", prop("integer", "Vertical delta")}},
             {"dy"}),
      [this](const QJsonObject &args) {
        const int dx = args.value("dx").toInt(0), dy = args.value("dy").toInt();
        return onCore([&]() -> ToolResult {
          QString err;
          auto *pc = localPlatform(m_app, err);
          if (pc == nullptr)
            return ToolResult::error(err);
          pc->fakeMouseWheel(ScrollDelta{dx, dy});
          return ToolResult::text(QStringLiteral("scrolled %1,%2").arg(dx).arg(dy));
        });
      });

  add("key_type", "Type a text string on this machine using the current keyboard layout.",
      schema({{"text", prop("string", "Text to type; \\n sends Return, \\t sends Tab")}}, {"text"}),
      [this](const QJsonObject &args) {
        const auto text = args.value("text").toString();
        return onCore([&]() -> ToolResult {
          QString err;
          auto *pc = localPlatform(m_app, err);
          if (pc == nullptr)
            return ToolResult::error(err);
          const auto ucs4 = text.toUcs4();
          for (const auto cp : ucs4) {
            KeyID id = static_cast<KeyID>(cp);
            if (cp == '\n' || cp == '\r')
              id = kKeyReturn;
            else if (cp == '\t')
              id = kKeyTab;
            tap(pc, id, 0);
          }
          return ToolResult::text(QStringLiteral("typed %1 characters").arg(ucs4.size()));
        });
      });

  add("key_press", "Press a key combination such as \"cmd+shift+4\", \"ctrl+c\", \"enter\" or \"f5\".",
      schema({{"keys", prop("string", "Combo joined by + ; modifiers: cmd, ctrl, alt, shift")}}, {"keys"}),
      [this](const QJsonObject &args) {
        std::vector<KeyID> mods;
        KeySpec key;
        QString err;
        if (!parseCombo(args.value("keys").toString(), mods, key, err))
          return ToolResult::error(err);
        return onCore([&]() -> ToolResult {
          QString perr;
          auto *pc = localPlatform(m_app, perr);
          if (pc == nullptr)
            return ToolResult::error(perr);
          std::vector<KeyButton> held;
          KeyModifierMask mask = 0;
          for (const auto m : mods) {
            const auto b = nextButton();
            pc->fakeKeyDown(m, mask, b, std::string());
            held.push_back(b);
            if (m == kKeyShift_L)
              mask |= KeyModifierShift;
            else if (m == kKeyControl_L)
              mask |= KeyModifierControl;
            else if (m == kKeyAlt_L)
              mask |= KeyModifierAlt;
            else if (m == kKeySuper_L)
              mask |= KeyModifierSuper;
          }
          tap(pc, key.id, mask);
          for (auto it = held.rbegin(); it != held.rend(); ++it)
            pc->fakeKeyUp(*it);
          return ToolResult::text(QStringLiteral("pressed %1").arg(args.value("keys").toString()));
        });
      });

  add("clipboard_get", "Read the text clipboard of this machine.", schema({}), [this](const QJsonObject &) {
    return onCore([&]() -> ToolResult {
      QString err;
      auto *pc = localPlatform(m_app, err);
      if (pc == nullptr)
        return ToolResult::error(err);
      Clipboard cb;
      if (!pc->getClipboard(kClipboardClipboard, &cb))
        return ToolResult::error(QStringLiteral("could not read clipboard"));
      // Clipboard::has/get only answer while the clipboard object is open.
      if (!cb.open(0))
        return ToolResult::error(QStringLiteral("could not open clipboard data"));
      const bool hasText = cb.has(IClipboard::Format::Text);
      const auto text = hasText ? cb.get(IClipboard::Format::Text) : std::string();
      cb.close();
      return ToolResult::text(QString::fromStdString(text));
    });
  });

  add("clipboard_set", "Replace the text clipboard of this machine.",
      schema({{"text", prop("string", "UTF-8 text")}}, {"text"}), [this](const QJsonObject &args) {
        const auto text = args.value("text").toString().toStdString();
        return onCore([&]() -> ToolResult {
          QString err;
          auto *pc = localPlatform(m_app, err);
          if (pc == nullptr)
            return ToolResult::error(err);
          Clipboard cb;
          cb.open(0);
          cb.empty();
          cb.add(IClipboard::Format::Text, text);
          cb.close();
          if (!pc->setClipboard(kClipboardClipboard, &cb))
            return ToolResult::error(QStringLiteral("could not write clipboard"));
          return ToolResult::text(QStringLiteral("clipboard set (%1 bytes)").arg(text.size()));
        });
      });
}

// ---------------------------------------------------------------------------
// Server-only tools

void McpTools::registerServerTools()
{
  add("list_computers", "List computers currently connected to this Deskflow server (including itself).", schema({}),
      [this](const QJsonObject &) {
        return onCore([&]() -> ToolResult {
          auto *server = dynamic_cast<ServerApp *>(m_app);
          if (server == nullptr || server->getServerPtr() == nullptr)
            return ToolResult::error(QStringLiteral("server not running"));
          std::vector<std::string> clients;
          server->getServerPtr()->getClients(clients);
          QJsonArray arr;
          for (const auto &c : clients)
            arr.append(QString::fromStdString(c));
          return ToolResult::json(QJsonObject{{"computers", arr}});
        });
      });

  add("switch_to_computer", "Move the shared keyboard and mouse to the named computer.",
      schema({{"name", prop("string", "Computer name as configured in Deskflow")}}, {"name"}),
      [this](const QJsonObject &args) {
        const auto name = args.value("name").toString().toStdString();
        return onCore([&]() -> ToolResult {
          auto *server = dynamic_cast<ServerApp *>(m_app);
          if (server == nullptr || server->getServerPtr() == nullptr)
            return ToolResult::error(QStringLiteral("server not running"));
          auto *info = new Server::SwitchToComputerInfo(name);
          m_bridge->events()->addEvent(Event(
              EventTypes::ServerSwitchToComputer, server->getServerPtr()->switchEventTarget(), info,
              Event::EventFlags::DeliverImmediately
          ));
          return ToolResult::text(QStringLiteral("switch requested: %1").arg(QString::fromStdString(name)));
        });
      });

  add("lock_to_computer", "Lock (true) or unlock (false) the cursor to the active computer.",
      schema({{"locked", prop("boolean", "true to lock, false to unlock")}}, {"locked"}),
      [this](const QJsonObject &args) {
        const bool locked = args.value("locked").toBool();
        return onCore([&]() -> ToolResult {
          auto *server = dynamic_cast<ServerApp *>(m_app);
          if (server == nullptr || server->getServerPtr() == nullptr)
            return ToolResult::error(QStringLiteral("server not running"));
          auto *info = new Server::LockCursorToComputerInfo(
              locked ? Server::LockCursorToComputerInfo::kOn : Server::LockCursorToComputerInfo::kOff
          );
          m_bridge->events()->addEvent(Event(
              EventTypes::ServerLockCursorToComputer, server->getServerPtr()->switchEventTarget(), info,
              Event::EventFlags::DeliverImmediately
          ));
          return ToolResult::text(locked ? QStringLiteral("locked") : QStringLiteral("unlocked"));
        });
      });
}

} // namespace deskflow::mcp
