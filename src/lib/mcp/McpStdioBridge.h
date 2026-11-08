/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Chimera Kang
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

namespace deskflow::mcp {

//! Run the stdio <-> MCP socket bridge: `deskflow-core mcp-bridge [--socket NAME]`.
/*!
Reads newline-delimited JSON-RPC from stdin, forwards it to the running core's
local MCP socket, and writes the replies to stdout. Needs no GUI and no Qt
event loop of the core, so it can be started from an ssh session. Returns the
process exit code.
*/
int runStdioBridge(int argc, char **argv);

} // namespace deskflow::mcp
