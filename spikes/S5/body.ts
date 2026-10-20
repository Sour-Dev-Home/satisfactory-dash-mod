/** The agentVersion the mod sends (AgentVersionSchema: 1-32 characters, agent.ts:39). */
export const S5_AGENT_VERSION = "spike-s5";

/** Shaped on satisfactory-dash's own valid fixture agentSnapshotRequestUnreachable (packages/shared/fixtures/agent.ts:70):
 *  the smallest snapshot the contract accepts. The C++ in Task 4 builds exactly this, with its own observedAt. */
export const s5SnapshotBody = (observedAt: string) => ({ agentVersion: S5_AGENT_VERSION, observedAt, reachable: false, paused: null });
