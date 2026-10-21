/** A precondition failed: nothing ran, and no evidence is written. (run.ts re-exports it; backend.ts uses it too.) */
export class RefusalError extends Error {}
