# Upstream tracking

- Upstream: https://github.com/deskflow/deskflow (GPL-2.0)
- Fork: https://github.com/HaticeStudio/deskflow, branch `mcp`
- Pinned upstream tag: `v1.27.0` (commit `081f6478`)
- Local checkout: `/Volumes/eclipse/projects/deskflow`

Update procedure:

```
git fetch upstream --tags
git rebase <new-tag> mcp
# fix conflicts, rebuild, run through docs/dev/mcp.md milestone 5 checklist
```

Files the fork touches outside `src/lib/mcp/` and `src/apps/deskflow-mcp/`:
`src/apps/deskflow-core/deskflow-core.cpp`, `src/lib/base/EventTypes.h`,
`src/lib/deskflow/App.cpp`, `src/lib/common/Settings.h`, `CMakeLists.txt`,
`src/lib/CMakeLists.txt`, `src/apps/CMakeLists.txt`.
