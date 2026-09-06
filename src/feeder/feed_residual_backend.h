#pragma once

// Non-D3D11 backends cannot compose a residual. Observe a newly enabled option
// after reloading but before any backend writes. While blocked, frames_done does
// not advance, so poll on a bounded clock instead of waiting for another frame.
// The caller remains responsible for its ordinary enabled/mode checks.
static void PollBlockedFeedConfig(ULONGLONG &next_poll, ULONGLONG now)
{
    if (now >= next_poll) {
        next_poll = now + 1000;
        if (CfgReload()) g.frame_ready = false;
    }
}

static bool ResidualBackendAllowsReplacement()
{
    static ULONGLONG next_blocked_poll = 0;
    const ULONGLONG now = GetTickCount64();
    if (g_cfg.work_composite) {
        PollBlockedFeedConfig(next_blocked_poll, now);
    } else {
        if ((g.frames_done % 60) == 0 && CfgReload()) g.frame_ready = false;
        if (g_cfg.work_composite) next_blocked_poll = now + 1000;
    }
    if (g_cfg.work_composite) {
        ResidualPreserveOriginal("work_composite is implemented only for D3D11");
        return false;
    }
    next_blocked_poll = 0;
    return true;
}
