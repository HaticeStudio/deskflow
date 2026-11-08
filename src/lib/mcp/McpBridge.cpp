/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Chimera Kang
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "McpBridge.h"

#include "base/EventTypes.h"
#include "base/IEventQueue.h"
#include "base/Log.h"

#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>

namespace deskflow::mcp {

namespace {
struct WaitState
{
  std::mutex mutex;
  std::condition_variable cv;
  bool done = false;
};
} // namespace

McpBridge::McpBridge(IEventQueue *events) : m_events(events)
{
  // Handler runs on whichever thread dispatches the core event queue.
  m_events->addHandler(EventTypes::McpToolCall, this, [](const Event &event) {
    if (const auto *data = dynamic_cast<const ToolCallData *>(event.getDataObject()); data != nullptr) {
      data->run();
    } else {
      LOG_WARN("mcp bridge got tool call event without payload");
    }
  });
}

McpBridge::~McpBridge()
{
  // The core EventQueue may already be gone when Qt tears down the McpServer
  // (QApplication destroys its children after main()'s locals). Removing the
  // handler is only a courtesy, so never let it throw out of a destructor.
  try {
    if (m_events != nullptr)
      m_events->removeHandler(EventTypes::McpToolCall, this);
  } catch (...) { // NOSONAR - destructor must not throw
  }
}

void McpBridge::detach()
{
  try {
    if (m_events != nullptr)
      m_events->removeHandler(EventTypes::McpToolCall, this);
  } catch (...) { // NOSONAR
  }
  m_events = nullptr;
}

bool McpBridge::runOnCore(const std::function<void()> &fn, int timeoutMs) const
{
  auto state = std::make_shared<WaitState>();
  auto *data = new ToolCallData([state, fn] {
    fn();
    std::scoped_lock lock{state->mutex};
    state->done = true;
    state->cv.notify_all();
  });

  // The event queue takes ownership of data and deletes it after dispatch.
  m_events->addEvent(Event(EventTypes::McpToolCall, const_cast<McpBridge *>(this), data));

  std::unique_lock lock{state->mutex};
  return state->cv.wait_for(lock, std::chrono::milliseconds(timeoutMs), [&] { return state->done; });
}

} // namespace deskflow::mcp
