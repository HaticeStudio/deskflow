/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Chimera Kang
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "base/Event.h"

#include <functional>

class IEventQueue;

namespace deskflow::mcp {

//! Event payload carrying a closure to run on the core thread.
class ToolCallData : public EventData
{
public:
  explicit ToolCallData(std::function<void()> fn) : m_fn(std::move(fn))
  {
  }
  ~ToolCallData() override = default;
  void run() const
  {
    if (m_fn)
      m_fn();
  }

private:
  std::function<void()> m_fn;
};

//! Runs closures on the core (event queue) thread and waits for them.
/*!
The platform screen and the Server/Client objects must only be touched from
the thread that dispatches the core EventQueue. McpBridge posts an
EventTypes::McpToolCall event whose handler executes the closure there, and
blocks the caller until it finishes or the timeout expires.
*/
class McpBridge
{
public:
  explicit McpBridge(IEventQueue *events);
  ~McpBridge();
  McpBridge(const McpBridge &) = delete;
  McpBridge &operator=(const McpBridge &) = delete;

  //! Execute \p fn on the core thread. Returns false if it did not run within \p timeoutMs.
  bool runOnCore(const std::function<void()> &fn, int timeoutMs) const;

  IEventQueue *events() const
  {
    return m_events;
  }

private:
  IEventQueue *m_events;
};

} // namespace deskflow::mcp
